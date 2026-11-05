#ifndef ROOT_CAR_CLIENT_H
#define ROOT_CAR_CLIENT_H
#include "Communication.h"
#include "CarProtocol.h"
/* Call only from Root's communication task (the queue owner). */
typedef struct {
    uint32_t session, receivedAt;
    uint16_t window;
    uint8_t validSeconds;
    bool ready;
} RootCarClient;
bool RootCarClient_OnMessage(RootCarClient *client, const CommunicationMessage_t *message, uint32_t now);
bool RootCarClient_Request(Communication_t *comm, uint8_t requestType);
bool RootCarClient_Start(RootCarClient *client, Communication_t *comm, uint32_t now, uint32_t requestId);
bool RootCarClient_SetTimeout(RootCarClient *client, Communication_t *comm, uint32_t now, uint16_t seconds);
bool RootCarClient_Stop(RootCarClient *client, Communication_t *comm, uint32_t now);
#endif
