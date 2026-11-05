#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdbool.h>
#include <stdint.h>

#include "CommunicationLink.h"
#include "SecureCommunication.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMMUNICATION_TX_QUEUE_DEPTH 4u
#define COMMUNICATION_RX_QUEUE_DEPTH 3u
#define COMMUNICATION_MAX_ROUTES     16u
#define COMMUNICATION_SEND_TIMEOUT_MS 30u

typedef struct
{
    uint8_t sourceId;
    uint8_t destinationId;
    SecureMessageType_t messageType;
    uint16_t payloadLength;
    uint8_t payload[SECURE_TRANSPORT_MAX_MESSAGE_SIZE];
} CommunicationMessage_t;

typedef struct
{
    bool inUse;
    uint8_t logicalDeviceId;
    CommunicationPhysicalAddress_t physicalAddress;
} CommunicationRoute_t;

typedef enum
{
    COMMUNICATION_OK = 0,
    COMMUNICATION_IDLE,
    COMMUNICATION_NOT_INITIALIZED,
    COMMUNICATION_INVALID_ARGUMENT,
    COMMUNICATION_ROUTE_NOT_FOUND,
    COMMUNICATION_TX_QUEUE_FULL,
    COMMUNICATION_RX_QUEUE_FULL,
    COMMUNICATION_SECURE_ERROR,
    COMMUNICATION_PROCESS_LINK_ERROR
} CommunicationStatus_t;

typedef struct
{
    SecureCommunication_t *secure;
    CommunicationLink_t link;
    CommunicationRoute_t routes[COMMUNICATION_MAX_ROUTES];
    CommunicationMessage_t txQueue[COMMUNICATION_TX_QUEUE_DEPTH];
    CommunicationMessage_t rxQueue[COMMUNICATION_RX_QUEUE_DEPTH];
    uint8_t txHead, txCount, rxHead, rxCount;
    CommunicationLinkStatus_t lastLinkStatus;
    SecureTransportStatus_t lastSecureStatus;
    bool initialized;
} Communication_t;

bool Communication_Init(Communication_t *context,
                        SecureCommunication_t *secure,
                        const CommunicationLink_t *link);

bool Communication_AddRoute(Communication_t *context,
                            uint8_t logicalDeviceId,
                            const uint8_t *physicalAddress,
                            uint8_t physicalAddressLength);

bool Communication_Send(Communication_t *context,
                        uint8_t destinationId,
                        SecureMessageType_t messageType,
                        const void *payload,
                        uint16_t payloadLength);

CommunicationStatus_t Communication_Process(Communication_t *context);
/* Used during relay pulses: sending does not write receive counters to NOR. */
CommunicationStatus_t Communication_ProcessTxOnly(Communication_t *context);
bool Communication_TryReceive(Communication_t *context,
                              CommunicationMessage_t *message);
bool Communication_IsInitialized(const Communication_t *context);
bool Communication_IsTxQueueEmpty(const Communication_t *context);
bool Communication_ResetTxQueue(Communication_t *context);
CommunicationLinkStatus_t Communication_GetLastLinkStatus(const Communication_t *context);
SecureTransportStatus_t Communication_GetLastSecureStatus(const Communication_t *context);
const char *Communication_StatusToString(CommunicationStatus_t status);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_H */
