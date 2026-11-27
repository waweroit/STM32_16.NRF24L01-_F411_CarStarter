#include "CarStarter.h"
#include "CarConfig.h"
#include "CarEngine.h"
#include "CarButton.h"
#include "CarSensors.h"
#include "CarPower.h"
#include "NrfLink.h"
#include "CarProtocol.h"
#include "CarStorage.h"
#include "Communication.h"
#include "CommunicationDevices.h"
#include "UsbDebug.h"
#include "spi.h"
#include "rtc.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static W25Q_HandleTypeDef flash;
static CarEngine engine;
static NrfLink_t radio;
static SecureCommunication_t secure;
static Communication_t communication;
static bool storageOk, secureOk, radioOk, reporting, tempRequested;
static bool radioPowered, radioWindowStarted;
static bool debugReplyPending, thresholdReplyPending;
static const char *storageError = "storage state";
static uint32_t storageLogAt;
static uint32_t cycleAt, radioPowerAt, reportAt, radioAttemptAt, readyAt, errorAt;
static bool buttonStopPending;
static uint32_t buttonStopRequestedAt, buttonStopWaitAt;
static uint16_t window;
static enum { REPORT_TELEMETRY, REPORT_TIMEOUT, REPORT_READY } reportStep;
_Static_assert(CAR_KEY_SETTLE_MS+CAR_BUTTON_PULSE_MS+CAR_START_OBSERVE_MS<CAR_ENGINE_RESULT_MS,
               "START must fit in the result deadline");
_Static_assert(CAR_BUTTON_PULSE_MS+CAR_STOP_VERIFY_MS<CAR_ENGINE_RESULT_MS,
               "STOP must fit in the result deadline");
typedef struct { CarResult code; uint32_t request, rootSession; uint8_t command; } PendingResult;
static PendingResult results[8];
static uint8_t resultHead, resultCount;
static uint32_t remoteStopRequest, remoteStopSession;
/** Zwraca konfigurację węzła Root, z którym komunikuje się sterownik. */
static const CommunicationDevice_t *Root(void) { return &communicationDevices[CAR_ROOT_INDEX]; }
/** Zwraca lokalną konfigurację CarStarter używaną do inicjalizacji sesji i radia. */
static const CommunicationDevice_t *Self(void) { return &communicationDevices[CAR_DEVICE_INDEX]; }
/** Ustawia zasilanie kluczyka. Wartość true argumentu on włącza wyjście. */
static void Key(bool on) { HAL_GPIO_WritePin(CarKeyPower_GPIO_Port, CarKeyPower_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
/** Steruje wyjściem przycisku Start/Stop. Wartość true argumentu on odpowiada wciśnięciu. */
static void Button(bool on) { HAL_GPIO_WritePin(StartEngineStopCarButton_GPIO_Port, StartEngineStopCarButton_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
/** Loguje wynik code polecenia command powiązany z identyfikatorem request i odkłada go do
 * wysłania, jeśli w kolejce jest miejsce. */
static void QueueResult(uint8_t command, CarResult code, uint32_t request, uint32_t rootSession)
{
    if (UsbDebug_IsVerbose()) DebugPrintf("Car result cmd=%02X result=%u request=%lu\r\n", command, (unsigned)code, (unsigned long)request);
    if (resultCount < 8u) {
        results[(resultHead+resultCount)%8u] = (PendingResult){code,request,rootSession,command}; ++resultCount;
    }
}
static void EngineResult(CarResult code, uint32_t request)
{
    uint8_t command=0u;
    uint32_t rootSession=0u;
    if (code==CAR_RESULT_STARTED || code==CAR_RESULT_START_FAILED) {
        command=CAR_MSG_START;
        reporting=true; reportStep=REPORT_TELEMETRY;
    } else if (code==CAR_RESULT_STOPPED || code==CAR_RESULT_STOP_FAILED) {
        command=remoteStopRequest ? CAR_MSG_STOP : 0u;
        request=remoteStopRequest;
        rootSession=remoteStopSession;
        remoteStopRequest=remoteStopSession=0u;
        reporting=true; reportStep=REPORT_TELEMETRY;
        if (UsbDebug_IsVerbose()) DebugPrintf("CAR STOP verify: voltage=%umV adc=%s stopped_below=%umV\r\n",
            CarSensors_Millivolts(),CarSensors_VoltageValid(HAL_GetTick())?"valid":"invalid",engine.stoppedMv);
    }
    QueueResult(command,code,request,rootSession);
}
/** Próbuje zakolejkować do Root wiadomość typu type zawierającą length bajtów z data. Zwraca
 * true po przyjęciu przez warstwę komunikacji. */
static bool Send(SecureMessageType_t type, const uint8_t *data, uint16_t length)
{
    return radioOk && Communication_Send(&communication, Root()->deviceId, type, data, length);
}
/* Read back the actual receiver configuration after returning from TX. */
static void LogRadioState(void)
{
    const uint8_t registers[] = {0x00u, 0x01u, 0x02u, 0x08u, 0x17u, 0x1Du, 0x1Cu, 0x11u};
    uint8_t values[sizeof(registers)], address[NRF_LINK_ADDRESS_SIZE];
    for (unsigned i=0u; i<sizeof(registers); ++i) {
        if (NRF24_ReadRegister(&radio.radio, registers[i], &values[i]) != NRF24_OK) {
            Debug("CAR radio diagnostic SPI read failed\r\n");
            return;
        }
    }
    if (NRF24_ReadBuffer(&radio.radio, 0x0Au, address, sizeof(address)) != NRF24_OK) {
        Debug("CAR radio diagnostic address read failed\r\n");
        return;
    }
    DebugPrintf("CAR radio CONFIG=%02X EN_AA=%02X EN_RXADDR=%02X OBSERVE_TX=%02X FIFO=%02X CE=%u RX0=%02X%02X%02X%02X%02X FEATURE=%02X DYNPD=%02X RX_PW_P0=%02X\r\n",
        values[0], values[1], values[2], values[3], values[4],
        HAL_GPIO_ReadPin(NRF24_CE_GPIO_Port, NRF24_CE_Pin)==GPIO_PIN_SET,
        address[0], address[1], address[2], address[3], address[4],
        values[5], values[6], values[7]);
    if (radio.txFailureSnapshotValid) {
        const uint8_t *tx=radio.txFailureAddress, *ack=radio.txFailureAckAddress;
        DebugPrintf("CAR TX snapshot t=%lums CONFIG=%02X TX=%02X%02X%02X%02X%02X ACK_RX0=%02X%02X%02X%02X%02X\r\n",
            (unsigned long)radio.txFailureAt, radio.txFailureConfig,
            tx[0], tx[1], tx[2], tx[3], tx[4], ack[0], ack[1], ack[2], ack[3], ack[4]);
    }
}
/** Próbuje zakolejkować READY z identyfikatorem sesji i okna oraz liczbą pełnych sekund
 * pozostałych na polecenie. Zwraca wynik kolejkowania. */
static bool ReadyMessage(void)
{
    uint8_t data[8];
    Car_Put32(data, secure.localSessionId); Car_Put16(data+4, window);
    uint32_t age = HAL_GetTick()-cycleAt;
    data[6] = age >= CAR_LISTEN_MS ? 0u : (uint8_t)((CAR_LISTEN_MS-age)/1000u);
    data[7] = 1u; /* protocol version */
    return Send(MESSAGE_TYPE_CAR_READY, data, sizeof(data));
}
/** Buduje raport o silniku, czujnikach i licznikach. now to czas HAL w ms używany do oceny
 * świeżości napięcia i czasu do zgaszenia. Zwraca wynik kolejkowania. */
static bool Telemetry(uint32_t now)
{
    uint8_t data[11];
    data[0] = 1u;
    data[1] = (uint8_t)engine.status;
    data[2] = (CarSensors_VoltageValid(now) ? 1u:0u) |
              (CarSensors_Temperature()!=INT16_MIN ? 2u:0u) |
              (engine.autoStop ? 4u:0u) | (engine.buttonStop ? 8u:0u) |
              (engine.phase!=CAR_IDLE ? 16u:0u) | (storageOk ? 0u:32u);
    Car_Put16(data+3, CarSensors_Millivolts());
    Car_Put16(data+5, (uint16_t)CarSensors_Temperature());
    Car_Put16(data+7, CarEngine_Remaining(&engine, now));
    Car_Put16(data+9, (uint16_t)engine.timeoutSeconds);
    return Send(MESSAGE_TYPE_CAR_TELEMETRY, data, sizeof(data));
}
/** Wysyła do kolejki raport z ustawionym czasem automatycznego gaszenia w sekundach i zwraca
 * wynik kolejkowania. */
static bool TimeoutMessage(void)
{
    uint8_t data[2]; Car_Put16(data, (uint16_t)engine.timeoutSeconds);
    return Send(MESSAGE_TYPE_CAR_TIMEOUT, data, sizeof(data));
}
static bool SendThresholds(void)
{
    uint8_t data[4];
    Car_Put16(data,engine.runningMv); Car_Put16(data+2,engine.stoppedMv);
    return Send(MESSAGE_TYPE_CAR_THRESHOLDS,data,sizeof(data));
}
/** Sprawdza pola sesji i okna w pierwszych sześciu bajtach p oraz termin ważności okna
 * względem now (ms). */
static bool TokenValid(const uint8_t *p, uint32_t now)
{
    return Car_Get32(p)==secure.localSessionId && Car_Get16(p+4)==window && now-cycleAt<CAR_LISTEN_MS;
}
static bool RenewRadioWindow(uint32_t now)
{
    if (window == UINT16_MAX) {
        storageError = "radio window limit";
        storageOk = false;
        return false;
    }
    ++window;
    cycleAt = now;
    readyAt = now - 10000u;
    return true;
}

static void OpenRadioWindow(void)
{
    cycleAt = HAL_GetTick();
    if (!secureOk || !RenewRadioWindow(cycleAt)) return;
    HAL_GPIO_WritePin(DEV_SHUTDOWN_SW_GPIO_Port, DEV_SHUTDOWN_SW_Pin, GPIO_PIN_SET);
    radioPowered = true;
    radioPowerAt = cycleAt;
    radioWindowStarted = false;
    if (UsbDebug_IsVerbose()) DebugPrintf("CAR window=%u opening t=%lums listen=%lums\r\n",
        (unsigned)window, (unsigned long)cycleAt, (unsigned long)CAR_LISTEN_MS);
    radioAttemptAt = cycleAt - 5000u;
}

static void CloseRadioWindow(void)
{
    if (UsbDebug_IsVerbose()) DebugPrintf("CAR window=%u closing t=%lums\r\n", (unsigned)window, (unsigned long)HAL_GetTick());
    HAL_GPIO_WritePin(NRF24_CE_GPIO_Port, NRF24_CE_Pin, GPIO_PIN_RESET);
    if (radioOk) {
        NRF24_Status_t status=NRF24_PowerDown(&radio.radio);
        if (status!=NRF24_OK)
            DebugPrintf("Radio power-down: %s\r\n",NRF24_StatusToString(status));
    }
    HAL_GPIO_WritePin(DEV_SHUTDOWN_SW_GPIO_Port, DEV_SHUTDOWN_SW_Pin, GPIO_PIN_RESET);
    radioPowered = false;
    radioWindowStarted = false;
    radioOk = false;
}

/** Uruchamia radio dla przygotowanej sesji i przy pierwszym uruchomieniu dodaje trasę do
 * Root. Zwraca true, gdy łącze jest gotowe. */
static bool StartRadio(void)
{
    if (!secureOk) return false;
    NRF24_Status_t status = NrfLink_Init(&radio, &hspi1, NRF24_CE_GPIO_Port, NRF24_CE_Pin,
        SPI1_NRF24_CS_GPIO_Port, SPI1_NRF24_CS_Pin, Self()->nrfAddress);
    if (status != NRF24_OK) {
        DebugPrintf("Radio init: %s\r\n", NRF24_StatusToString(status));
        if (UsbDebug_IsVerbose()) LogRadioState();
        return false;
    }
    uint8_t rfSetup;
    NRF24_Status_t readStatus = NRF24_ReadRegister(&radio.radio, 0x06u, &rfSetup);
    if (readStatus != NRF24_OK) {
        DebugPrintf("WARN NRF RF_SETUP read failed: %s\r\n", NRF24_StatusToString(readStatus));
    } else if ((rfSetup & 0x2Eu) != 0x26u) {
        DebugPrintf("WARN NRF settings not accepted: RF_SETUP=0x%02X, expected 250 kbps / 0 dBm\r\n",
                    (unsigned)rfSetup);
    }

    /* Preserve queues and secure counters across STOP/radio recovery. */
    if (!Communication_IsInitialized(&communication)) {
        CommunicationLink_t link = NrfLink_AsCommunicationLink(&radio);
        if (!Communication_Init(&communication, &secure, &link)) return false;
    }
    return Communication_AddRoute(&communication, Root()->deviceId, Root()->nrfAddress, NRF_LINK_ADDRESS_SIZE);
}
/** Weryfikuje polecenie START i zleca rozruch silnika.
 * message zawiera identyfikatory sesji i okna, ID żądania oraz opcjonalny czas pracy w minutach.
 * now to czas HAL w ms używany w pierwszych sprawdzeniach. Po zapisie do pamięci funkcja
 * pobiera bieżący czas i ponownie sprawdza ważność okna.
 * Zapisuje ID do *request, jeśli długość danych jest prawidłowa.
 * Zwraca wynik przyjęcia polecenia lub przyczynę odrzucenia. */
static CarResult StartFromMessage(const CommunicationMessage_t *message, uint32_t now, uint32_t *request)
{
    if (message->payloadLength!=10u && message->payloadLength!=11u) return CAR_RESULT_BAD_REQUEST;
    const uint8_t *p=message->payload;
    *request=Car_Get32(p+6);
    uint8_t minutes=message->payloadLength==11u ? p[10] : 0u;
    if (!TokenValid(p,now)) return CAR_RESULT_STALE_WINDOW;
    if (!*request || (message->payloadLength==11u && (!minutes || minutes>CAR_START_MAX_MINUTES)))
        return CAR_RESULT_BAD_REQUEST;
    if (!storageOk || !CarStorage_Ready()) return CAR_RESULT_STORAGE_ERROR;
    CarStoredData data=*CarStorage_Get();
    if (data.lastStartRequest==*request) return CAR_RESULT_DUPLICATE;
    if (engine.phase!=CAR_IDLE || engine.autoStop || engine.buttonStop) return CAR_RESULT_BUSY;
    if (!CarSensors_VoltageValid(now)) return CAR_RESULT_SENSOR_ERROR;
    if (engine.status!=CAR_ENGINE_STOPPED) return CAR_RESULT_ALREADY_RUNNING;

    data.lastStartRequest=*request;
    if (minutes) data.stopSeconds=(uint32_t)minutes*60u;
    if (!CarStorage_Save(&data)) { storageOk=false; return CAR_RESULT_STORAGE_ERROR; }
    CarEngine_SetTimeout(&engine,data.stopSeconds);
    now=HAL_GetTick();
    if (!TokenValid(p,now)) return CAR_RESULT_STALE_WINDOW;
    return CarEngine_Start(&engine,now,CarSensors_Millivolts(),CarSensors_VoltageValid(now),*request);
}

/** Obsługuje odebraną wiadomość m z warstwy komunikacji. now to czas odbioru w ms używany do
 * sprawdzenia okna polecenia i rozpoczęcia gaszenia. */
static void Handle(const CommunicationMessage_t *m, uint32_t now)
{
    if (m->sourceId != Root()->deviceId) return;
    const uint8_t *p=m->payload;
    CarResult result=CAR_RESULT_BAD_REQUEST;
    uint32_t request=0u, rootSession=0u;
    switch (m->messageType) {
    case MESSAGE_TYPE_CAR_SET_DEBUG:
        if (m->payloadLength!=1u || p[0]>1u) break;
        UsbDebug_SetVerbose(p[0]!=0u);
        debugReplyPending=true;
        return;
    case MESSAGE_TYPE_CAR_GET_TEMPERATURE:
        if (m->payloadLength) break;
        CarSensors_RequestTemperature(); tempRequested=true; return;
    case MESSAGE_TYPE_CAR_GET_VOLTAGE:
    case MESSAGE_TYPE_CAR_GET_STATUS:
        if (m->payloadLength) break;
        reporting=true; reportStep=REPORT_TELEMETRY; return;
    case MESSAGE_TYPE_CAR_GET_TIMEOUT:
        if (m->payloadLength) break;
        if (!TimeoutMessage()) QueueResult(m->messageType, CAR_RESULT_BUSY, 0u, 0u);
        return;
    case MESSAGE_TYPE_CAR_GET_THRESHOLDS:
        if (m->payloadLength) break;
        thresholdReplyPending=true;
        return;
    case MESSAGE_TYPE_CAR_START:
        result=StartFromMessage(m,now,&request);
        break;
    case MESSAGE_TYPE_CAR_SET_TIMEOUT:
        if (m->payloadLength!=8u) break;
        if (!TokenValid(p,now)) { result=CAR_RESULT_STALE_WINDOW; break; }
        {
            uint16_t seconds=Car_Get16(p+6);
            if (seconds<CAR_MIN_STOP_SECONDS || seconds>CAR_MAX_STOP_SECONDS) break;
            CarStoredData data=*CarStorage_Get(); data.stopSeconds=seconds;
            if (!storageOk || !CarStorage_Save(&data)) { storageOk=false; result=CAR_RESULT_STORAGE_ERROR; break; }
            CarEngine_SetTimeout(&engine, seconds); result=CAR_RESULT_TIMEOUT_SAVED;
        }
        break;
    case MESSAGE_TYPE_CAR_SET_THRESHOLDS:
        if (m->payloadLength!=10u) break;
        if (!TokenValid(p,now)) { result=CAR_RESULT_STALE_WINDOW; break; }
        {
            uint16_t runningMv=Car_Get16(p+6), stoppedMv=Car_Get16(p+8);
            if (!Car_ThresholdsValid(runningMv,stoppedMv)) break;
            if (engine.phase!=CAR_IDLE || engine.autoStop || engine.buttonStop || buttonStopPending) {
                result=CAR_RESULT_BUSY; break;
            }
            CarStoredData data=*CarStorage_Get(); data.runningMv=runningMv; data.stoppedMv=stoppedMv;
            if (!storageOk || !CarStorage_Save(&data)) { storageOk=false; result=CAR_RESULT_STORAGE_ERROR; break; }
            CarEngine_SetThresholds(&engine,runningMv,stoppedMv);
            thresholdReplyPending=true;
            return;
        }
    case MESSAGE_TYPE_CAR_STOP:
        rootSession=CarStorage_Get()->rootSession; /* Set by the authenticated receive path. */
        if (m->payloadLength!=10u) break;
        request=Car_Get32(p+6);
        if (!request) break;
        if (!TokenValid(p,now)) { result=CAR_RESULT_STALE_WINDOW; break; }
        result=CarEngine_Stop(&engine, now, CarSensors_VoltageValid(now));
        if (result==CAR_RESULT_ACCEPTED) {
            remoteStopRequest=request;
            remoteStopSession=rootSession;
        }
        break;
    default: return;
    }
    QueueResult(m->messageType,result,request,rootSession);
}
/** Próbuje wysłać oczekujące wyniki, kolejne części raportu i odpowiedź temperaturową. now
 * to czas HAL w ms używany także do ponawiania READY. */
static void ProcessOutgoing(uint32_t now)
{
    if (debugReplyPending) {
        uint8_t enabled=UsbDebug_IsVerbose() ? 1u : 0u;
        if (Send(MESSAGE_TYPE_CAR_SET_DEBUG,&enabled,sizeof(enabled))) debugReplyPending=false;
    }
    if (resultCount) {
        PendingResult *r=&results[resultHead];
        uint8_t data[10]={r->command,(uint8_t)r->code}; Car_Put32(data+2,r->request);
        uint16_t length=6u;
        if (r->command==CAR_MSG_STOP) { Car_Put32(data+6,r->rootSession); length=10u; }
        if (Send(MESSAGE_TYPE_CAR_RESULT,data,length)) { resultHead=(resultHead+1u)%8u; --resultCount; }
    }
    if (thresholdReplyPending && SendThresholds()) thresholdReplyPending=false;
    if (reporting && CarSensors_TemperatureReady()) {
        switch (reportStep) {
        case REPORT_TELEMETRY:
            if (Telemetry(now)) reportStep=REPORT_TIMEOUT;
            break;
        case REPORT_TIMEOUT:
            if (TimeoutMessage()) reportStep=REPORT_READY;
            break;
        case REPORT_READY:
            if (now-cycleAt>=CAR_LISTEN_MS || ReadyMessage()) { reporting=false; readyAt=now; }
            break;
        }
    }
    if (tempRequested && CarSensors_TemperatureReady()) {
        /* Existing TEMPERATURE type is ASCII Celsius, as in MeasurementUnit. */
        int16_t t=CarSensors_Temperature(); char text[16];
        int n;
        if (t==INT16_MIN) n=snprintf(text,sizeof(text),"ERR");
        else { int value=t; n=snprintf(text,sizeof(text),"%s%d.%02d",value<0?"-":"",abs(value)/100,abs(value)%100); }
        if (n>0 && Send(MESSAGE_TYPE_TEMPERATURE,(const uint8_t *)text,(uint16_t)n)) tempRequested=false;
    }
    if (now-cycleAt<CAR_LISTEN_MS && !reporting && now-readyAt>=10000u && ReadyMessage()) readyAt=now;
}
/** Przygotowuje wyjścia i czujniki, odczytuje ustawienia z flash i rozpoczyna pierwsze okno
 * komunikacji z Root. */
void CarStarter_Init(void)
{
    UsbDebug_Init();
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc); /* Arm only when actually sleeping. */
    CarEngineIo io={Key,Button,EngineResult};
    CarEngine_Init(&engine,&io,CAR_DEFAULT_STOP_SECONDS);
    CarButton_Init();
    HAL_GPIO_WritePin(BLUE_LED_GPIO_Port,BLUE_LED_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA,SPI1_NRF24_CS_Pin|SPI1_EEPROM_CS_Pin,GPIO_PIN_SET);
    HAL_Delay(20u);
    storageError = "W25Q64 init (SPI1, CS=PA4)";
    storageOk=W25Q_Init(&flash,&hspi1,SPI1_EEPROM_CS_GPIO_Port,SPI1_EEPROM_CS_Pin);
    if (storageOk) {
        storageError = "journal scan (first 8 KiB)";
        storageOk = CarStorage_Init(&flash);
    }
    if (storageOk) {
        storageError = "journal update";
        CarEngine_SetTimeout(&engine,CarStorage_Get()->stopSeconds);
#if !CAR_USE_DEFAULT_THRESHOLDS_AT_BOOT
        CarEngine_SetThresholds(&engine,CarStorage_Get()->runningMv,CarStorage_Get()->stoppedMv);
#endif
        secureOk=SecureCommunication_Init(&secure,Self()->deviceId,Self()->deviceKey) &&
            SecureCommunication_AuthorizePeer(&secure,Root()->deviceId,Root()->deviceKey);
        storageOk=CarStorage_Ready();
    }
    CarSensors_Init();
    OpenRadioWindow();
    reportAt=cycleAt; reporting=true;
    DebugPrintf("CarStarter ID=%02X storage=%u secure=%u timeout=%lus running_at=%umV stopped_below=%umV\r\n",
        Self()->deviceId,storageOk,secureOk,(unsigned long)engine.timeoutSeconds,
        engine.runningMv,engine.stoppedMv);
}
/** Wykonuje jeden przebieg pętli aplikacji. Obsługuje pomiary, przycisk, sterowanie
 * silnikiem i radio oraz podejmuje decyzję o uśpieniu. */
void CarStarter_Process(void)
{
    uint32_t now=HAL_GetTick();
    CarSensors_Process(now);
    CarClick click=CarButton_Process(now);
    if (click==CAR_CLICK_SINGLE) CarEngine_SingleClick(&engine);
    else if (click==CAR_CLICK_DOUBLE && engine.phase==CAR_IDLE) {
        buttonStopPending=true;
        buttonStopRequestedAt=CarButton_EventAt();
        buttonStopWaitAt=now;
    }
    else if (click==CAR_CLICK_LONG) Debug("Button long press\r\n");
    /* A recognized cancel click takes precedence over a simultaneous expiry. */
    uint8_t previousResultCount = resultCount;
    CarEngine_Process(&engine,now,CarSensors_Millivolts(),CarSensors_VoltageValid(now));
    /* Refresh engine status first. After STOP the ADC may not be ready yet. */
    if (buttonStopPending) {
        if (engine.phase!=CAR_IDLE || now-buttonStopWaitAt>=2500u) {
            buttonStopPending=false;
            Debug("Button double rejected: busy or ADC timeout\r\n");
        } else if (CarSensors_VoltageValid(now)) {
            CarEngine_DoubleClick(&engine,buttonStopRequestedAt);
            buttonStopPending=false;
            Debug(engine.buttonStop ? "Button double: stop timer armed\r\n" :
                                      "Button double rejected: engine not running\r\n");
            reporting=true; reportStep=REPORT_TELEMETRY;
        }
    }
    if (click!=CAR_CLICK_NONE) { reporting=true; reportStep=REPORT_TELEMETRY; }
    bool reportDue = now-reportAt >= CAR_REPORT_MS;
    if (reportDue) {
        reportAt=now; CarSensors_RequestTemperature(); reporting=true; reportStep=REPORT_TELEMETRY;
    }
    bool engineAwake = CarEngine_NeedsAwake(&engine);
    bool canOpenForEngine = engineAwake && secureOk && window<UINT16_MAX;
    if (!radioPowered && (canOpenForEngine || reportDue || click!=CAR_CLICK_NONE || resultCount!=previousResultCount)) {
        OpenRadioWindow();
        now=HAL_GetTick();
    }
    if (radioOk && engineAwake && now-cycleAt>=CAR_LISTEN_MS) RenewRadioWindow(now);
    uint32_t radioAge = now-radioPowerAt;
    if (radioPowered && !radioOk && radioAge>=CAR_RADIO_SETTLE_MS &&
        now-cycleAt<CAR_LISTEN_MS && now-radioAttemptAt>=5000u) {
        radioAttemptAt=now; radioOk=StartRadio();
        if (radioOk && !radioWindowStarted) {
            /* Supply settling and initialization precede the full command window. */
            cycleAt=HAL_GetTick();
            readyAt=cycleAt-10000u;
            radioWindowStarted=true;
        }
    }
    if (radioOk) {
        /* NOR sector erases are bounded but can take hundreds of ms. Defer RX
         * (and persistent replay writes) until timing-critical relay phases end. */
        bool canReceive=(engine.phase==CAR_IDLE || engine.phase==CAR_STOP_VERIFY) &&
            HAL_GetTick()-cycleAt<CAR_LISTEN_MS;
        CommunicationStatus_t status=canReceive ? Communication_Process(&communication) :
            Communication_ProcessTxOnly(&communication);
        if (status==COMMUNICATION_SECURE_ERROR || status==COMMUNICATION_PROCESS_LINK_ERROR) {
            if (now-errorAt>=1000u) {
                DebugPrintf("Communication %s / %s / %s t=%lums\r\n",Communication_StatusToString(status),
                    CommunicationLink_StatusToString(Communication_GetLastLinkStatus(&communication)),
                    SecureProtocol_StatusToString(SecureCommunication_GetLastProtocolStatus(&secure)),
                    (unsigned long)HAL_GetTick());
                if (UsbDebug_IsVerbose()) LogRadioState();
                errorAt=now;
            }
            CommunicationLinkStatus_t linkStatus=Communication_GetLastLinkStatus(&communication);
            if (linkStatus==COMMUNICATION_LINK_ERROR || linkStatus==COMMUNICATION_LINK_TIMEOUT) radioOk=false;
        }
        CommunicationMessage_t message;
        if (Communication_TryReceive(&communication,&message)) Handle(&message,HAL_GetTick());
    }
    storageOk=storageOk && CarStorage_Ready();
    now=HAL_GetTick();
    engineAwake = CarEngine_NeedsAwake(&engine);
    bool hold=buttonStopPending || engineAwake || CarButton_Busy() ||
              (CAR_USB_KEEP_AWAKE && CarPower_UsbActive());
    if (radioOk) ProcessOutgoing(now);
    if (radioPowered && now-cycleAt>=CAR_LISTEN_MS &&
        (!radioOk || ((!engineAwake || window==UINT16_MAX) && Communication_IsTxQueueEmpty(&communication) &&
         !reporting && !resultCount && !tempRequested && !debugReplyPending && !thresholdReplyPending))) {
        CloseRadioWindow();
    }
    if (!storageOk && now-storageLogAt>=5000u && CarPower_UsbActive()) {
        if (flash.initError) {
            DebugPrintf("W25Q64 PA4: %s", flash.initError);
            if (strcmp(flash.initError,"BUSY timeout")==0)
                DebugPrintf(" SR1=%02X", flash.lastStatus);
            else if (strcmp(flash.initError,"JEDEC mismatch")==0)
                DebugPrintf(" ID=%02X %02X %02X", flash.jedecId[0],flash.jedecId[1],flash.jedecId[2]);
            Debug("\r\n");
        } else {
            DebugPrintf("CarStarter storage error: %s\r\n", storageError);
        }
        storageLogAt=now;
    }
    UsbDebug_Process();
    if (!hold && !radioPowered && now-cycleAt>=CAR_LISTEN_MS && !CarSensors_Busy() && storageOk) {
        if (UsbDebug_IsVerbose()) Debug("STOP: 60s or user button\r\n");
        CarSleepResult sleepResult=CarPower_Sleep(&flash);
        if (sleepResult==CAR_FLASH_WAKE_ERROR) {
            /* Retry wake and verify the JEDEC ID before reusing the journal. */
            storageOk=W25Q_Init(&flash,&hspi1,SPI1_EEPROM_CS_GPIO_Port,SPI1_EEPROM_CS_Pin);
            if (!storageOk) {
                storageError = "W25Q64 wake";
                secureOk=false;
                Debug("Flash wake failed\r\n");
            }
        }
        OpenRadioWindow();
        reportAt=cycleAt;
        CarSensors_RequestTemperature(); reporting=true; reportStep=REPORT_TELEMETRY;
        if (sleepResult!=CAR_FLASH_WAKE_ERROR && UsbDebug_IsVerbose())
            Debug(sleepResult==CAR_SLEEP_COMPLETED?"Wake from STOP\r\n":"STOP cancelled\r\n");
    }
    HAL_GPIO_WritePin(BLUE_LED_GPIO_Port,BLUE_LED_Pin,storageOk?GPIO_PIN_SET:((now/250u)&1u?GPIO_PIN_SET:GPIO_PIN_RESET));
}
