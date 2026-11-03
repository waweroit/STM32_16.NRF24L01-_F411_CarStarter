#include "Communication.h"

#include <string.h>

static const CommunicationRoute_t *FindRoute(const Communication_t *context,
                                              uint8_t logicalDeviceId);
static CommunicationStatus_t ProcessRx(Communication_t *context);
static CommunicationStatus_t ProcessTx(Communication_t *context);

bool Communication_Init(Communication_t *context,
                        SecureCommunication_t *secure,
                        const CommunicationLink_t *link)
{
    if (context == NULL || secure == NULL || link == NULL ||
        !SecureCommunication_IsInitialized(secure) ||
        link->context == NULL || link->SendFrame == NULL ||
        link->ReceiveFrame == NULL)
    {
        return false;
    }

    memset(context, 0, sizeof(*context));
    context->secure = secure;
    context->link = *link;
    context->lastLinkStatus = COMMUNICATION_LINK_OK;
    context->lastSecureStatus = SECURE_TRANSPORT_OK;

    context->txQueue = osMessageQueueNew(COMMUNICATION_TX_QUEUE_DEPTH,
                                         sizeof(CommunicationMessage_t),
                                         NULL);
    context->rxQueue = osMessageQueueNew(COMMUNICATION_RX_QUEUE_DEPTH,
                                         sizeof(CommunicationMessage_t),
                                         NULL);

    if (context->txQueue == NULL || context->rxQueue == NULL)
    {
        return false;
    }

    context->initialized = true;
    return true;
}

bool Communication_AddRoute(Communication_t *context,
                            uint8_t logicalDeviceId,
                            const uint8_t *physicalAddress,
                            uint8_t physicalAddressLength)
{
    uint8_t i;

    if (context == NULL || !context->initialized || logicalDeviceId == 0u ||
        logicalDeviceId == context->secure->localDeviceId ||
        physicalAddress == NULL || physicalAddressLength == 0u ||
        physicalAddressLength > COMMUNICATION_PHYSICAL_ADDRESS_MAX_SIZE)
    {
        return false;
    }

    for (i = 0u; i < COMMUNICATION_MAX_ROUTES; ++i)
    {
        CommunicationRoute_t *route = &context->routes[i];

        if (route->inUse && route->logicalDeviceId == logicalDeviceId)
        {
            route->physicalAddress.length = physicalAddressLength;
            memset(route->physicalAddress.bytes, 0,
                   sizeof(route->physicalAddress.bytes));
            memcpy(route->physicalAddress.bytes,
                   physicalAddress,
                   physicalAddressLength);
            return true;
        }
    }

    for (i = 0u; i < COMMUNICATION_MAX_ROUTES; ++i)
    {
        CommunicationRoute_t *route = &context->routes[i];

        if (!route->inUse)
        {
            memset(route, 0, sizeof(*route));
            route->inUse = true;
            route->logicalDeviceId = logicalDeviceId;
            route->physicalAddress.length = physicalAddressLength;
            memcpy(route->physicalAddress.bytes,
                   physicalAddress,
                   physicalAddressLength);
            return true;
        }
    }

    return false;
}

bool Communication_Send(Communication_t *context,
                        uint8_t destinationId,
                        SecureMessageType_t messageType,
                        const void *payload,
                        uint16_t payloadLength)
{
    CommunicationMessage_t message;

    if (context == NULL || !context->initialized ||
        destinationId == 0u || destinationId == context->secure->localDeviceId ||
        payloadLength > SECURE_TRANSPORT_MAX_MESSAGE_SIZE ||
        (payloadLength > 0u && payload == NULL) ||
        FindRoute(context, destinationId) == NULL)
    {
        return false;
    }

    memset(&message, 0, sizeof(message));
    message.sourceId = context->secure->localDeviceId;
    message.destinationId = destinationId;
    message.messageType = messageType;
    message.payloadLength = payloadLength;

    if (payloadLength > 0u)
    {
        memcpy(message.payload, payload, payloadLength);
    }

    return osMessageQueuePut(context->txQueue, &message, 0u, 0u) == osOK;
}

CommunicationStatus_t Communication_Process(Communication_t *context)
{
    CommunicationStatus_t rxStatus;
    CommunicationStatus_t txStatus;

    if (context == NULL || !context->initialized)
    {
        return COMMUNICATION_NOT_INITIALIZED;
    }

    /* Keep the normal radio state in RX. Incoming traffic has priority. */
    rxStatus = ProcessRx(context);
    if (rxStatus != COMMUNICATION_OK && rxStatus != COMMUNICATION_IDLE)
    {
        return rxStatus;
    }

    txStatus = ProcessTx(context);
    if (txStatus != COMMUNICATION_IDLE)
    {
        return txStatus;
    }

    return rxStatus;
}

bool Communication_TryReceive(Communication_t *context,
                              CommunicationMessage_t *message)
{
    if (context == NULL || !context->initialized || message == NULL)
    {
        return false;
    }

    return osMessageQueueGet(context->rxQueue, message, NULL, 0u) == osOK;
}

bool Communication_IsInitialized(const Communication_t *context)
{
    return context != NULL && context->initialized;
}

bool Communication_IsTxQueueEmpty(const Communication_t *context)
{
    return context != NULL && context->initialized && context->txQueue != NULL && osMessageQueueGetCount(context->txQueue) == 0u;
}

bool Communication_ResetTxQueue(Communication_t *context)
{
    return context != NULL && context->initialized && context->txQueue != NULL && osMessageQueueReset(context->txQueue) == osOK;
}

CommunicationLinkStatus_t Communication_GetLastLinkStatus(const Communication_t *context)
{
    if (context == NULL)
    {
        return COMMUNICATION_LINK_INVALID_ARGUMENT;
    }

    return context->lastLinkStatus;
}

SecureTransportStatus_t Communication_GetLastSecureStatus(const Communication_t *context)
{
    if (context == NULL)
    {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    return context->lastSecureStatus;
}

const char *Communication_StatusToString(CommunicationStatus_t status)
{
    switch (status)
    {
    case COMMUNICATION_OK: return "OK";
    case COMMUNICATION_IDLE: return "IDLE";
    case COMMUNICATION_NOT_INITIALIZED: return "NOT_INITIALIZED";
    case COMMUNICATION_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case COMMUNICATION_ROUTE_NOT_FOUND: return "ROUTE_NOT_FOUND";
    case COMMUNICATION_TX_QUEUE_FULL: return "TX_QUEUE_FULL";
    case COMMUNICATION_RX_QUEUE_FULL: return "RX_QUEUE_FULL";
    case COMMUNICATION_SECURE_ERROR: return "SECURE_ERROR";
    case COMMUNICATION_PROCESS_LINK_ERROR: return "LINK_ERROR";
    default: return "UNKNOWN";
    }
}

static const CommunicationRoute_t *FindRoute(const Communication_t *context,
                                              uint8_t logicalDeviceId)
{
    uint8_t i;

    if (context == NULL)
    {
        return NULL;
    }

    for (i = 0u; i < COMMUNICATION_MAX_ROUTES; ++i)
    {
        const CommunicationRoute_t *route = &context->routes[i];

        if (route->inUse && route->logicalDeviceId == logicalDeviceId)
        {
            return route;
        }
    }

    return NULL;
}

static CommunicationStatus_t ProcessRx(Communication_t *context)
{
    uint8_t frame[SECURE_MAX_FRAME_SIZE];
    uint8_t frameLength = 0u;
    uint16_t messageLength = 0u;
    uint8_t sourceId = 0u;
    SecureMessageType_t messageType = MESSAGE_TYPE_NONE;
    SecureTransportStatus_t secureStatus;
    CommunicationLinkStatus_t linkStatus;
    CommunicationMessage_t message;

    linkStatus = context->link.ReceiveFrame(context->link.context,
                                            frame,
                                            (uint8_t)sizeof(frame),
                                            &frameLength);
    context->lastLinkStatus = linkStatus;

    if (linkStatus != COMMUNICATION_LINK_OK)
    {
        return COMMUNICATION_PROCESS_LINK_ERROR;
    }

    if (frameLength == 0u)
    {
        return COMMUNICATION_IDLE;
    }

    memset(&message, 0, sizeof(message));

    secureStatus = SecureCommunication_ProcessFrame(context->secure,
                                                    frame,
                                                    frameLength,
                                                    &messageType,
                                                    message.payload,
                                                    sizeof(message.payload),
                                                    &messageLength,
                                                    &sourceId);
    context->lastSecureStatus = secureStatus;

    if (secureStatus == SECURE_TRANSPORT_IN_PROGRESS)
    {
        return COMMUNICATION_OK;
    }

    if (secureStatus != SECURE_TRANSPORT_OK)
    {
        return COMMUNICATION_SECURE_ERROR;
    }

    message.sourceId = sourceId;
    message.destinationId = context->secure->localDeviceId;
    message.messageType = messageType;
    message.payloadLength = messageLength;

    if (osMessageQueuePut(context->rxQueue, &message, 0u, 0u) != osOK)
    {
        return COMMUNICATION_RX_QUEUE_FULL;
    }

    return COMMUNICATION_OK;
}

static CommunicationStatus_t ProcessTx(Communication_t *context)
{
    CommunicationMessage_t message;
    const CommunicationRoute_t *route;
    SecureCommunicationTxState_t txState;
    SecureTransportStatus_t secureStatus;
    bool messageComplete = false;

    if (osMessageQueueGet(context->txQueue, &message, NULL, 0u) != osOK)
    {
        return COMMUNICATION_IDLE;
    }

    route = FindRoute(context, message.destinationId);
    if (route == NULL)
    {
        return COMMUNICATION_ROUTE_NOT_FOUND;
    }

    secureStatus = SecureCommunication_BeginMessage(context->secure,
                                                    message.destinationId,
                                                    message.messageType,
                                                    message.payload,
                                                    message.payloadLength,
                                                    &txState);
    context->lastSecureStatus = secureStatus;

    if (secureStatus != SECURE_TRANSPORT_OK)
    {
        return COMMUNICATION_SECURE_ERROR;
    }

    while (!messageComplete)
    {
        uint8_t frame[SECURE_MAX_FRAME_SIZE];
        uint8_t frameLength = 0u;
        CommunicationLinkStatus_t linkStatus;

        secureStatus = SecureCommunication_CreateNextFrame(context->secure,
                                                           &txState,
                                                           frame,
                                                           (uint8_t)sizeof(frame),
                                                           &frameLength,
                                                           &messageComplete);
        context->lastSecureStatus = secureStatus;

        if (secureStatus != SECURE_TRANSPORT_OK)
        {
            return COMMUNICATION_SECURE_ERROR;
        }

        linkStatus = context->link.SendFrame(context->link.context,
                                             &route->physicalAddress,
                                             frame,
                                             frameLength,
                                             COMMUNICATION_SEND_TIMEOUT_MS);
        context->lastLinkStatus = linkStatus;

        if (linkStatus != COMMUNICATION_LINK_OK)
        {
            return COMMUNICATION_PROCESS_LINK_ERROR;
        }
    }

    return COMMUNICATION_OK;
}
