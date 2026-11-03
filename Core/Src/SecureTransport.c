#include "SecureTransport.h"
#include <string.h>

#define FRAGMENT_MESSAGE_ID_OFFSET 0u
#define FRAGMENT_ORIGINAL_TYPE_OFFSET 1u
#define FRAGMENT_INDEX_OFFSET 2u
#define FRAGMENT_COUNT_OFFSET 3u
#define FRAGMENT_DATA_OFFSET SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE

_Static_assert(SECURE_TRANSPORT_FRAGMENT_DATA_SIZE > 0u,
               "Transport fragment header must fit SecureProtocol payload");
_Static_assert(SECURE_TRANSPORT_MAX_FRAGMENTS <= 64u,
               "receivedBitmap supports at most 64 fragments");
_Static_assert(SECURE_TRANSPORT_MAX_FRAGMENTS <= UINT8_MAX,
               "fragmentCount is encoded in one byte");

static uint8_t allocate_message_id(SecureTransportContext_t *context)
{
    uint8_t value = context->nextMessageId;

    if (value == 0u) {
        value = 1u;
    }
    context->nextMessageId = (uint8_t)(value + 1u);
    if (context->nextMessageId == 0u) {
        context->nextMessageId = 1u;
    }
    return value;
}

static SecureTransportReassembly_t *find_reassembly(SecureTransportContext_t *context,
                                                     uint8_t sourceId)
{
    size_t i;
    SecureTransportReassembly_t *freeSlot = NULL;

    for (i = 0u; i < SECURE_MAX_PEERS; ++i) {
        if (context->reassembly[i].inUse &&
            context->reassembly[i].sourceId == sourceId) {
            return &context->reassembly[i];
        }
        if (!context->reassembly[i].inUse && freeSlot == NULL) {
            freeSlot = &context->reassembly[i];
        }
    }
    return freeSlot;
}

static void initialize_reassembly(SecureTransportReassembly_t *slot,
                                  uint8_t sourceId,
                                  uint8_t messageId,
                                  SecureMessageType_t messageType,
                                  uint8_t fragmentCount)
{
    memset(slot, 0, sizeof(*slot));
    slot->inUse = true;
    slot->sourceId = sourceId;
    slot->messageId = messageId;
    slot->messageType = messageType;
    slot->fragmentCount = fragmentCount;
}

SecureTransportStatus_t SecureTransport_Init(SecureTransportContext_t *context,
                                              SecureProtocolContext_t *protocol)
{
    if (context == NULL || protocol == NULL) {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(*context));
    context->protocol = protocol;
    context->nextMessageId = 1u;
    context->lastProtocolStatus = SECURE_PROTOCOL_OK;
    return SECURE_TRANSPORT_OK;
}

SecureTransportStatus_t SecureTransport_BeginMessage(SecureTransportContext_t *context,
                                                      uint8_t destinationId,
                                                      SecureMessageType_t messageType,
                                                      const uint8_t *message,
                                                      uint16_t messageLength,
                                                      SecureTransportTxState_t *state)
{
    uint16_t fragmentCount;

    if (context == NULL || context->protocol == NULL || state == NULL ||
        destinationId == 0u ||
        (messageLength > 0u && message == NULL)) {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }
    if (messageType == MESSAGE_TYPE_FRAGMENT) {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }
    if (messageLength > SECURE_TRANSPORT_MAX_MESSAGE_SIZE) {
        return SECURE_TRANSPORT_MESSAGE_TOO_LARGE;
    }

    memset(state, 0, sizeof(*state));
    state->active = true;
    state->destinationId = destinationId;
    state->messageType = messageType;
    state->message = message;
    state->messageLength = messageLength;

    if (messageLength <= SECURE_MAX_PAYLOAD_SIZE) {
        state->fragmented = false;
        state->fragmentCount = 1u;
        return SECURE_TRANSPORT_OK;
    }

    fragmentCount = (uint16_t)((messageLength + SECURE_TRANSPORT_FRAGMENT_DATA_SIZE - 1u) /
                               SECURE_TRANSPORT_FRAGMENT_DATA_SIZE);
    if (fragmentCount == 0u || fragmentCount > SECURE_TRANSPORT_MAX_FRAGMENTS) {
        state->active = false;
        return SECURE_TRANSPORT_MESSAGE_TOO_LARGE;
    }

    state->fragmented = true;
    state->messageId = allocate_message_id(context);
    state->fragmentCount = (uint8_t)fragmentCount;
    return SECURE_TRANSPORT_OK;
}

SecureTransportStatus_t SecureTransport_CreateNextFrame(SecureTransportContext_t *context,
                                                         SecureTransportTxState_t *state,
                                                         uint8_t *serializedFrame,
                                                         uint8_t serializedFrameBufferSize,
                                                         uint8_t *serializedFrameLength,
                                                         bool *messageComplete)
{
    SecureProtocolStatus_t protocolStatus;

    if (context == NULL || context->protocol == NULL || state == NULL ||
        serializedFrame == NULL || serializedFrameLength == NULL ||
        messageComplete == NULL) {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }
    if (!state->active) {
        return SECURE_TRANSPORT_NO_ACTIVE_TX;
    }

    *messageComplete = false;

    if (!state->fragmented) {
        protocolStatus = SecureProtocol_CreateFrame(context->protocol,
                                                    state->destinationId,
                                                    state->messageType,
                                                    state->message,
                                                    (uint8_t)state->messageLength,
                                                    serializedFrame,
                                                    serializedFrameBufferSize,
                                                    serializedFrameLength);
        context->lastProtocolStatus = protocolStatus;
        if (protocolStatus != SECURE_PROTOCOL_OK) {
            return SECURE_TRANSPORT_PROTOCOL_ERROR;
        }
        state->active = false;
        *messageComplete = true;
        return SECURE_TRANSPORT_OK;
    }

    {
        uint8_t fragmentPayload[SECURE_MAX_PAYLOAD_SIZE];
        uint16_t offset = (uint16_t)state->nextFragmentIndex *
                          (uint16_t)SECURE_TRANSPORT_FRAGMENT_DATA_SIZE;
        uint16_t remaining = state->messageLength - offset;
        uint8_t fragmentDataLength = (remaining > SECURE_TRANSPORT_FRAGMENT_DATA_SIZE) ?
                                     (uint8_t)SECURE_TRANSPORT_FRAGMENT_DATA_SIZE :
                                     (uint8_t)remaining;
        uint8_t fragmentPayloadLength = (uint8_t)(SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE +
                                                  fragmentDataLength);

        fragmentPayload[FRAGMENT_MESSAGE_ID_OFFSET] = state->messageId;
        fragmentPayload[FRAGMENT_ORIGINAL_TYPE_OFFSET] = (uint8_t)state->messageType;
        fragmentPayload[FRAGMENT_INDEX_OFFSET] = state->nextFragmentIndex;
        fragmentPayload[FRAGMENT_COUNT_OFFSET] = state->fragmentCount;
        memcpy(&fragmentPayload[FRAGMENT_DATA_OFFSET],
               &state->message[offset], fragmentDataLength);

        protocolStatus = SecureProtocol_CreateFrame(context->protocol,
                                                    state->destinationId,
                                                    MESSAGE_TYPE_FRAGMENT,
                                                    fragmentPayload,
                                                    fragmentPayloadLength,
                                                    serializedFrame,
                                                    serializedFrameBufferSize,
                                                    serializedFrameLength);
        context->lastProtocolStatus = protocolStatus;
        if (protocolStatus != SECURE_PROTOCOL_OK) {
            return SECURE_TRANSPORT_PROTOCOL_ERROR;
        }

        ++state->nextFragmentIndex;
        if (state->nextFragmentIndex >= state->fragmentCount) {
            state->active = false;
            *messageComplete = true;
        }
    }

    return SECURE_TRANSPORT_OK;
}

SecureTransportStatus_t SecureTransport_ProcessFrame(SecureTransportContext_t *context,
                                                      const uint8_t *serializedFrame,
                                                      uint8_t serializedFrameLength,
                                                      SecureMessageType_t *messageType,
                                                      uint8_t *message,
                                                      uint16_t messageBufferSize,
                                                      uint16_t *messageLength,
                                                      uint8_t *sourceDeviceId)
{
    uint8_t plainPayload[SECURE_MAX_PAYLOAD_SIZE];
    uint8_t plainLength = 0u;
    uint8_t sourceId = 0u;
    SecureMessageType_t secureMessageType = MESSAGE_TYPE_NONE;
    SecureProtocolStatus_t protocolStatus;

    if (context == NULL || context->protocol == NULL || serializedFrame == NULL ||
        messageType == NULL || messageLength == NULL || sourceDeviceId == NULL ||
        (messageBufferSize > 0u && message == NULL)) {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    protocolStatus = SecureProtocol_ProcessFrame(context->protocol,
                                                 serializedFrame,
                                                 serializedFrameLength,
                                                 &secureMessageType,
                                                 plainPayload,
                                                 sizeof(plainPayload),
                                                 &plainLength,
                                                 &sourceId);
    context->lastProtocolStatus = protocolStatus;
    if (protocolStatus != SECURE_PROTOCOL_OK) {
        return SECURE_TRANSPORT_PROTOCOL_ERROR;
    }

    if (secureMessageType != MESSAGE_TYPE_FRAGMENT) {
        if (messageBufferSize < plainLength) {
            return SECURE_TRANSPORT_BUFFER_TOO_SMALL;
        }
        if (plainLength > 0u) {
            memcpy(message, plainPayload, plainLength);
        }
        *messageType = secureMessageType;
        *messageLength = plainLength;
        *sourceDeviceId = sourceId;
        return SECURE_TRANSPORT_OK;
    }

    if (plainLength <= SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE) {
        return SECURE_TRANSPORT_INVALID_FRAGMENT;
    }

    {
        uint8_t fragmentMessageId = plainPayload[FRAGMENT_MESSAGE_ID_OFFSET];
        SecureMessageType_t originalMessageType =
            (SecureMessageType_t)plainPayload[FRAGMENT_ORIGINAL_TYPE_OFFSET];
        uint8_t fragmentIndex = plainPayload[FRAGMENT_INDEX_OFFSET];
        uint8_t fragmentCount = plainPayload[FRAGMENT_COUNT_OFFSET];
        uint8_t fragmentDataLength = (uint8_t)(plainLength -
                                               SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE);
        uint16_t offset;
        SecureTransportReassembly_t *slot;
        uint64_t completeMask;

        if (fragmentMessageId == 0u || originalMessageType == MESSAGE_TYPE_FRAGMENT ||
            fragmentCount < 2u || fragmentCount > SECURE_TRANSPORT_MAX_FRAGMENTS ||
            fragmentIndex >= fragmentCount || fragmentDataLength == 0u ||
            fragmentDataLength > SECURE_TRANSPORT_FRAGMENT_DATA_SIZE) {
            return SECURE_TRANSPORT_INVALID_FRAGMENT;
        }
        if (fragmentIndex < (uint8_t)(fragmentCount - 1u) &&
            fragmentDataLength != SECURE_TRANSPORT_FRAGMENT_DATA_SIZE) {
            return SECURE_TRANSPORT_INVALID_FRAGMENT;
        }

        offset = (uint16_t)fragmentIndex * (uint16_t)SECURE_TRANSPORT_FRAGMENT_DATA_SIZE;
        if ((uint16_t)(offset + fragmentDataLength) > SECURE_TRANSPORT_MAX_MESSAGE_SIZE) {
            return SECURE_TRANSPORT_INVALID_FRAGMENT;
        }

        slot = find_reassembly(context, sourceId);
        if (slot == NULL) {
            return SECURE_TRANSPORT_REASSEMBLY_FULL;
        }
        if (!slot->inUse || slot->sourceId != sourceId ||
            slot->messageId != fragmentMessageId) {
            initialize_reassembly(slot, sourceId, fragmentMessageId,
                                  originalMessageType, fragmentCount);
        } else if (slot->messageType != originalMessageType ||
                   slot->fragmentCount != fragmentCount) {
            memset(slot, 0, sizeof(*slot));
            return SECURE_TRANSPORT_INVALID_FRAGMENT;
        }

        memcpy(&slot->data[offset], &plainPayload[FRAGMENT_DATA_OFFSET],
               fragmentDataLength);
        slot->receivedBitmap |= ((uint64_t)1u << fragmentIndex);
        if (fragmentIndex == (uint8_t)(fragmentCount - 1u)) {
            slot->lastFragmentLength = fragmentDataLength;
        }

        completeMask = ((uint64_t)1u << fragmentCount) - 1u;
        if ((slot->receivedBitmap & completeMask) != completeMask) {
            return SECURE_TRANSPORT_IN_PROGRESS;
        }

        if (slot->lastFragmentLength == 0u) {
            memset(slot, 0, sizeof(*slot));
            return SECURE_TRANSPORT_INVALID_FRAGMENT;
        }

        {
            uint16_t totalLength =
                (uint16_t)(fragmentCount - 1u) *
                (uint16_t)SECURE_TRANSPORT_FRAGMENT_DATA_SIZE +
                slot->lastFragmentLength;

            if (totalLength > messageBufferSize) {
                memset(slot, 0, sizeof(*slot));
                return SECURE_TRANSPORT_BUFFER_TOO_SMALL;
            }
            memcpy(message, slot->data, totalLength);
            *messageType = slot->messageType;
            *messageLength = totalLength;
            *sourceDeviceId = sourceId;
            memset(slot, 0, sizeof(*slot));
        }
    }

    return SECURE_TRANSPORT_OK;
}

SecureProtocolStatus_t SecureTransport_GetLastProtocolStatus(
    const SecureTransportContext_t *context)
{
    return (context != NULL) ? context->lastProtocolStatus :
                               SECURE_PROTOCOL_INVALID_ARGUMENT;
}

void SecureTransport_ResetPeerReassembly(SecureTransportContext_t *context,
                                         uint8_t sourceDeviceId)
{
    size_t i;

    if (context == NULL) {
        return;
    }
    for (i = 0u; i < SECURE_MAX_PEERS; ++i) {
        if (context->reassembly[i].inUse &&
            context->reassembly[i].sourceId == sourceDeviceId) {
            memset(&context->reassembly[i], 0, sizeof(context->reassembly[i]));
            return;
        }
    }
}

const char *SecureTransport_StatusToString(SecureTransportStatus_t status)
{
    switch (status) {
    case SECURE_TRANSPORT_OK: return "OK";
    case SECURE_TRANSPORT_IN_PROGRESS: return "IN_PROGRESS";
    case SECURE_TRANSPORT_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case SECURE_TRANSPORT_MESSAGE_TOO_LARGE: return "MESSAGE_TOO_LARGE";
    case SECURE_TRANSPORT_INVALID_FRAGMENT: return "INVALID_FRAGMENT";
    case SECURE_TRANSPORT_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case SECURE_TRANSPORT_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
    case SECURE_TRANSPORT_REASSEMBLY_FULL: return "REASSEMBLY_FULL";
    case SECURE_TRANSPORT_NO_ACTIVE_TX: return "NO_ACTIVE_TX";
    default: return "UNKNOWN";
    }
}
