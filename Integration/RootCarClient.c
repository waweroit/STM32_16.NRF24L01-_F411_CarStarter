/* Optional integration source for STM32_16.NRF24L01 _F411_Root.
 * Copy this file/header and Core/Inc/CarProtocol.h into Root. This directory is
 * deliberately outside CarStarter's CubeIDE source folders. */
#include "RootCarClient.h"
#define CAR_ID 0x03u
/** Rozpoznaje wiadomość m od CarStarter. Dla poprawnej ramki READY zapisuje w kliencie c
 * parametry okna i czas odbioru now (ms). Zwraca true dla każdej wiadomości od CarStarter
 * przy poprawnych wskaźnikach. */
bool RootCarClient_OnMessage(RootCarClient *c, const CommunicationMessage_t *m, uint32_t now)
{
    if (!c || !m || m->sourceId!=CAR_ID) return false;
    if ((uint8_t)m->messageType==CAR_MSG_READY && m->payloadLength==8u && m->payload[7]==1u) {
        c->session=Car_Get32(m->payload); c->window=Car_Get16(m->payload+4);
        c->validSeconds=m->payload[6]; c->receivedAt=now;
        c->ready=c->validSeconds>1u;
    }
    return true;
}
/** Na podstawie stanu klienta c i czasu now (ms) sprawdza ważność okna. Przy powodzeniu
 * zapisuje sześć bajtów sesji i numeru okna do bufora p. */
static bool Token(RootCarClient *c, uint32_t now, uint8_t *p)
{
    /* Reserve one second for RF transmission/processing. CarStarter checks its
     * own deadline too, so a delayed queue can only cause rejection. */
    if (!c || !c->ready || c->validSeconds<=1u ||
        now-c->receivedAt >= (uint32_t)(c->validSeconds-1u)*1000u) return false;
    Car_Put32(p,c->session); Car_Put16(p+4,c->window); return true;
}
/** Kolejkuje przez comm zapytanie typu type o temperaturę, napięcie, stan lub czas gaszenia.
 * Zwraca true po przyjęciu zapytania do kolejki. */
bool RootCarClient_Request(Communication_t *comm, uint8_t type)
{
    if (type!=CAR_MSG_GET_TEMPERATURE && type!=CAR_MSG_GET_VOLTAGE &&
        type!=CAR_MSG_GET_STATUS && type!=CAR_MSG_GET_TIMEOUT && type!=CAR_MSG_GET_THRESHOLDS) return false;
    return Communication_Send(comm,CAR_ID,(SecureMessageType_t)type,NULL,0u);
}
/** Kolejkuje przez comm żądanie startu z niezerowym identyfikatorem id. Używa okna klienta c
 * ważnego w chwili now (ms). Zwraca wynik przyjęcia żądania do kolejki. */
bool RootCarClient_Start(RootCarClient *c, Communication_t *comm, uint32_t now, uint32_t id)
{
    uint8_t data[10];
    if (!id || !Token(c,now,data)) return false;
    Car_Put32(data+6,id);
    return Communication_Send(comm,CAR_ID,(SecureMessageType_t)CAR_MSG_START,data,sizeof(data));
}
/** Kolejkuje przez comm zmianę czasu gaszenia na seconds (30–3600 s), używając okna klienta
 * c sprawdzonego w chwili now (ms). Zwraca true po przyjęciu polecenia do kolejki. */
bool RootCarClient_SetTimeout(RootCarClient *c, Communication_t *comm, uint32_t now, uint16_t seconds)
{
    uint8_t data[8];
    if (seconds<30u || seconds>3600u || !Token(c,now,data)) return false;
    Car_Put16(data+6,seconds);
    return Communication_Send(comm,CAR_ID,(SecureMessageType_t)CAR_MSG_SET_TIMEOUT,data,sizeof(data));
}
bool RootCarClient_SetThresholds(RootCarClient *c, Communication_t *comm, uint32_t now,
                                uint16_t runningMv, uint16_t stoppedMv)
{
    uint8_t data[10];
    if (!Car_ThresholdsValid(runningMv,stoppedMv) || !Token(c,now,data)) return false;
    Car_Put16(data+6,runningMv); Car_Put16(data+8,stoppedMv);
    return Communication_Send(comm,CAR_ID,(SecureMessageType_t)CAR_MSG_SET_THRESHOLDS,data,sizeof(data));
}
/** Kolejkuje przez comm polecenie gaszenia, używając okna klienta c ważnego w chwili now
 * (ms). Zwraca true po przyjęciu polecenia do kolejki. */
bool RootCarClient_Stop(RootCarClient *c, Communication_t *comm, uint32_t now, uint32_t requestId)
{
    uint8_t data[10];
    if (!requestId || !Token(c,now,data)) return false;
    Car_Put32(data+6,requestId);
    return Communication_Send(comm,CAR_ID,(SecureMessageType_t)CAR_MSG_STOP,data,sizeof(data));
}
