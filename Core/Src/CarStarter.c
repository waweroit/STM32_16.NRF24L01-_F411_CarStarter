#include "CarStarter.h"
#include "CarConfig.h"
#include "CarEngine.h"
#include "CarButton.h"
#include "CarSensors.h"
#include "CarPower.h"
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
static uint32_t cycleAt, reportAt, radioAttemptAt, readyAt, windowAt, errorAt;
static uint16_t window;
static unsigned reportStep;
typedef struct { CarResult code; uint32_t request; uint8_t command; } PendingResult;
static PendingResult results[8];
static uint8_t resultHead, resultCount;
static uint8_t activeCommand;
static const CommunicationDevice_t *Root(void) { return &communicationDevices[CAR_ROOT_INDEX]; }
static const CommunicationDevice_t *Self(void) { return &communicationDevices[CAR_DEVICE_INDEX]; }
static void Key(bool on) { HAL_GPIO_WritePin(CarKeyPower_GPIO_Port, CarKeyPower_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void Button(bool on) { HAL_GPIO_WritePin(StartEngineStopCarButton_GPIO_Port, StartEngineStopCarButton_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void QueueResult(uint8_t command, CarResult code, uint32_t request)
{
    DebugPrintf("Car result cmd=%02X result=%u request=%lu\r\n", command, (unsigned)code, (unsigned long)request);
    if (resultCount < 8u) {
        results[(resultHead+resultCount)%8u] = (PendingResult){code,request,command}; ++resultCount;
    }
}
static void EngineResult(CarResult code, uint32_t request) { QueueResult(activeCommand, code, request); }
static bool Send(SecureMessageType_t type, const uint8_t *data, uint16_t length)
{
    return radioOk && Communication_Send(&communication, Root()->deviceId, type, data, length);
}
static bool ReadyMessage(void)
{
    uint8_t data[8];
    Car_Put32(data, secure.localSessionId); Car_Put16(data+4, window);
    uint32_t age = HAL_GetTick()-windowAt;
    data[6] = age >= CAR_LISTEN_MS ? 0u : (uint8_t)((CAR_LISTEN_MS-age)/1000u);
    data[7] = 1u; /* protocol version */
    return Send(MESSAGE_TYPE_CAR_READY, data, sizeof(data));
}
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
static bool TimeoutMessage(void)
{
    uint8_t data[2]; Car_Put16(data, (uint16_t)engine.timeoutSeconds);
    return Send(MESSAGE_TYPE_CAR_TIMEOUT, data, sizeof(data));
}
static bool TokenValid(const uint8_t *p, uint32_t now)
{
    return Car_Get32(p)==secure.localSessionId && Car_Get16(p+4)==window && now-windowAt<CAR_LISTEN_MS;
}
static void NewWindow(uint32_t now)
{
    if (window == UINT16_MAX) {
        /* Never reuse a challenge within one boot. Reads/local timers continue. */
        storageOk = false;
        return;
    }
    ++window; windowAt = now; readyAt = now-10000u;
}
static bool StartRadio(void)
{
    if (!secureOk) return false;
    NRF24_Status_t status = NrfLink_Init(&radio, &hspi1, NRF24_CE_GPIO_Port, NRF24_CE_Pin,
        SPI1_NRF24_CS_GPIO_Port, SPI1_NRF24_CS_Pin, Self()->nrfAddress);
    if (status != NRF24_OK) { DebugPrintf("Radio init: %s\r\n", NRF24_StatusToString(status)); return false; }
    /* Preserve queues and secure counters across STOP/radio recovery. */
    if (!Communication_IsInitialized(&communication)) {
        CommunicationLink_t link = NrfLink_AsCommunicationLink(&radio);
        if (!Communication_Init(&communication, &secure, &link) ||
            !Communication_AddRoute(&communication, Root()->deviceId, Root()->nrfAddress, NRF_LINK_ADDRESS_SIZE)) return false;
    }
    return true;
}
static void Handle(const CommunicationMessage_t *m, uint32_t now)
{
    if (m->sourceId != Root()->deviceId) return;
    const uint8_t *p=m->payload;
    CarResult result=CAR_RESULT_BAD_REQUEST;
    uint32_t request=0u;
    switch (m->messageType) {
    case MESSAGE_TYPE_CAR_GET_TEMPERATURE:
        if (m->payloadLength) break;
        CarSensors_RequestTemperature(); tempRequested=true; return;
    case MESSAGE_TYPE_CAR_GET_VOLTAGE:
    case MESSAGE_TYPE_CAR_GET_STATUS:
        if (m->payloadLength) break;
        reporting=true; reportStep=0u; return;
    case MESSAGE_TYPE_CAR_GET_TIMEOUT:
        if (m->payloadLength) break;
        if (!TimeoutMessage()) QueueResult(m->messageType, CAR_RESULT_BUSY, 0u);
        return;
    case MESSAGE_TYPE_CAR_START:
        if (m->payloadLength!=10u) break;
        request=Car_Get32(p+6);
        if (!TokenValid(p,now)) { result=CAR_RESULT_STALE_WINDOW; break; }
        if (!request) break;
        if (!storageOk || !CarStorage_Ready()) { result=CAR_RESULT_STORAGE_ERROR; break; }
        if (CarStorage_Get()->lastStartRequest==request) { result=CAR_RESULT_DUPLICATE; break; }
        if (engine.phase!=CAR_IDLE || engine.autoStop || engine.buttonStop) { result=CAR_RESULT_BUSY; break; }
        if (!CarSensors_VoltageValid(now)) { result=CAR_RESULT_SENSOR_ERROR; break; }
        if (engine.status!=CAR_ENGINE_STOPPED) { result=CAR_RESULT_ALREADY_RUNNING; break; }
        {
            CarStoredData data=*CarStorage_Get(); data.lastStartRequest=request;
            if (!CarStorage_Save(&data)) { storageOk=false; result=CAR_RESULT_STORAGE_ERROR; break; }
        }
        activeCommand=m->messageType;
        result=CarEngine_Start(&engine, HAL_GetTick(), CarSensors_Millivolts(), CarSensors_VoltageValid(HAL_GetTick()), request);
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
    case MESSAGE_TYPE_CAR_STOP:
        if (m->payloadLength!=6u) break;
        if (!TokenValid(p,now)) { result=CAR_RESULT_STALE_WINDOW; break; }
        activeCommand=m->messageType;
        result=CarEngine_Stop(&engine, now, CarSensors_VoltageValid(now));
        break;
    default: return;
    }
    QueueResult(m->messageType,result,request);
}
static void ProcessOutgoing(uint32_t now)
{
    if (resultCount) {
        PendingResult *r=&results[resultHead];
        uint8_t data[6]={r->command,(uint8_t)r->code}; Car_Put32(data+2,r->request);
        if (Send(MESSAGE_TYPE_CAR_RESULT,data,sizeof(data))) { resultHead=(resultHead+1u)%8u; --resultCount; }
    }
    if (reporting && CarSensors_TemperatureReady()) {
        if (reportStep==0u) { if (Telemetry(now)) ++reportStep; }
        else if (reportStep==1u) { if (TimeoutMessage()) ++reportStep; }
        else if (ReadyMessage()) { reporting=false; readyAt=now; }
    }
    if (tempRequested && CarSensors_TemperatureReady()) {
        /* Existing TEMPERATURE type is ASCII Celsius, as in MeasurementUnit. */
        int16_t t=CarSensors_Temperature(); char text[16];
        int n;
        if (t==INT16_MIN) n=snprintf(text,sizeof(text),"ERR");
        else { int value=t; n=snprintf(text,sizeof(text),"%s%d.%02d",value<0?"-":"",abs(value)/100,abs(value)%100); }
        if (n>0 && Send(MESSAGE_TYPE_TEMPERATURE,(const uint8_t *)text,(uint16_t)n)) tempRequested=false;
    }
    if (!reporting && now-readyAt>=10000u && ReadyMessage()) readyAt=now;
}
void CarStarter_Init(void)
{
    UsbDebug_Init();
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc); /* Arm only when actually sleeping. */
    CarEngineIo io={Key,Button,EngineResult};
    CarEngine_Init(&engine,&io,CAR_DEFAULT_STOP_SECONDS);
    CarButton_Init();
    HAL_GPIO_WritePin(BLUE_LED_GPIO_Port,BLUE_LED_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(DEV_SHUTDOWN_SW_GPIO_Port,DEV_SHUTDOWN_SW_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA,SPI1_NRF24_CS_Pin|SPI1_EEPROM_CS_Pin,GPIO_PIN_SET);
    HAL_Delay(20u);
    storageOk=W25Q_Init(&flash,&hspi1,SPI1_EEPROM_CS_GPIO_Port,SPI1_EEPROM_CS_Pin) && CarStorage_Init(&flash);
    if (storageOk) {
        CarEngine_SetTimeout(&engine,CarStorage_Get()->stopSeconds);
        secureOk=SecureCommunication_Init(&secure,Self()->deviceId,Self()->deviceKey) &&
            SecureCommunication_AuthorizePeer(&secure,Root()->deviceId,Root()->deviceKey);
        storageOk=CarStorage_Ready();
    }
    radioOk=StartRadio();
    CarSensors_Init();
    cycleAt=reportAt=HAL_GetTick();
    NewWindow(cycleAt); reporting=true;
    DebugPrintf("CarStarter ID=%02X storage=%u secure=%u radio=%u timeout=%lus\r\n",
        Self()->deviceId,storageOk,secureOk,radioOk,(unsigned long)engine.timeoutSeconds);
}
void CarStarter_Process(void)
{
    uint32_t now=HAL_GetTick();
    CarSensors_Process(now);
    CarClick click=CarButton_Process(now);
    if (click==CAR_CLICK_SINGLE) CarEngine_SingleClick(&engine);
    else if (click==CAR_CLICK_DOUBLE) CarEngine_DoubleClick(&engine,now);
    else if (click==CAR_CLICK_LONG) Debug("Button long press\r\n");
    /* A recognized cancel click takes precedence over a simultaneous expiry. */
    CarEngine_Process(&engine,now,CarSensors_Millivolts(),CarSensors_VoltageValid(now));
    if (click!=CAR_CLICK_NONE) { reporting=true; reportStep=0u; }
    if (secureOk && !radioOk && now-radioAttemptAt>=5000u) {
        radioAttemptAt=now; radioOk=StartRadio();
    }
    if (radioOk) {
        /* NOR sector erases are bounded but can take hundreds of ms. Defer RX
         * (and persistent replay writes) until timing-critical relay phases end. */
        CommunicationStatus_t status=engine.phase==CAR_IDLE ? Communication_Process(&communication) :
            Communication_ProcessTxOnly(&communication);
        if (status==COMMUNICATION_SECURE_ERROR || status==COMMUNICATION_PROCESS_LINK_ERROR) {
            if (now-errorAt>=1000u) {
                DebugPrintf("Communication %s / %s / %s\r\n",Communication_StatusToString(status),
                    CommunicationLink_StatusToString(Communication_GetLastLinkStatus(&communication)),
                    SecureProtocol_StatusToString(SecureCommunication_GetLastProtocolStatus(&secure)));
                errorAt=now;
            }
            if (Communication_GetLastLinkStatus(&communication)==COMMUNICATION_LINK_ERROR) radioOk=false;
        }
        CommunicationMessage_t message;
        if (Communication_TryReceive(&communication,&message)) Handle(&message,HAL_GetTick());
    }
    storageOk=storageOk && CarStorage_Ready();
    now=HAL_GetTick();
    if (now-reportAt>=CAR_REPORT_MS) {
        reportAt=now; CarSensors_RequestTemperature(); reporting=true; reportStep=0u;
    }
    bool hold=CarEngine_NeedsAwake(&engine) || CarButton_Busy() ||
              (CAR_USB_KEEP_AWAKE && CarPower_UsbActive());
    if (hold && now-windowAt>=CAR_LISTEN_MS) NewWindow(now);
    if (radioOk) ProcessOutgoing(now);
    UsbDebug_Process();
    if (!hold && now-cycleAt>=CAR_LISTEN_MS && !CarSensors_Busy() && storageOk &&
        (!radioOk || (Communication_IsTxQueueEmpty(&communication) && !reporting && !resultCount && !tempRequested))) {
        Debug("STOP: 60s or user button\r\n");
        bool slept=CarPower_Sleep(&flash,&radio,radioOk);
        /* Sleep preparation can power down the radio even if a button cancels WFI. */
        radioOk=false; radioOk=StartRadio();
        cycleAt=reportAt=HAL_GetTick(); NewWindow(cycleAt);
        CarSensors_RequestTemperature(); reporting=true; reportStep=0u;
        Debug(slept?"Wake from STOP\r\n":"STOP cancelled\r\n");
    }
    HAL_GPIO_WritePin(BLUE_LED_GPIO_Port,BLUE_LED_Pin,storageOk?GPIO_PIN_SET:((now/250u)&1u?GPIO_PIN_SET:GPIO_PIN_RESET));
}
