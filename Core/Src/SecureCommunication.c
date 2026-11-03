#include "SecureCommunication.h"

#include <string.h>
#include "main.h"
#include "SecureSessionStorage.h"

bool SecureCommunication_Init(SecureCommunication_t *context,
                              uint8_t localDeviceId,
                              const uint8_t deviceKey[CRYPTO_AES_KEY_SIZE])
{
    uint32_t uniqueId[3];
    uint32_t bootCounter;
    uint32_t sessionId = 0u;
    uint16_t entropySample;
    SecureProtocolStatus_t protocolStatus;
    SecureTransportStatus_t transportStatus;

    if (context == NULL || localDeviceId == 0u || deviceKey == NULL)
    {
        return false;
    }

    memset(context, 0, sizeof(*context));

    if (!SecureSessionStorage_NextBootCounter(&bootCounter))
    {
        return false;
    }

    uniqueId[0] = HAL_GetUIDw0();
    uniqueId[1] = HAL_GetUIDw1();
    uniqueId[2] = HAL_GetUIDw2();
    entropySample = (uint16_t)(uniqueId[0] ^ uniqueId[1] ^
                               uniqueId[2] ^ HAL_GetTick());

    protocolStatus = SecureProtocol_GenerateSessionId(deviceKey,
                                                       uniqueId,
                                                       bootCounter,
                                                       HAL_GetTick(),
                                                       entropySample,
                                                       &sessionId);
    if (protocolStatus != SECURE_PROTOCOL_OK)
    {
        return false;
    }

    protocolStatus = SecureProtocol_Init(&context->protocol,
                                         localDeviceId,
                                         sessionId,
                                         deviceKey);
    if (protocolStatus != SECURE_PROTOCOL_OK)
    {
        return false;
    }

    transportStatus = SecureTransport_Init(&context->transport,
                                           &context->protocol);
    if (transportStatus != SECURE_TRANSPORT_OK)
    {
        return false;
    }

    context->localDeviceId = localDeviceId;
    context->localSessionId = sessionId;
    context->initialized = true;
    return true;
}

bool SecureCommunication_AuthorizePeer(
    SecureCommunication_t *context,
    uint8_t peerDeviceId,
    const uint8_t peerKey[CRYPTO_AES_KEY_SIZE])
{
    if (context == NULL || !context->initialized || peerKey == NULL)
    {
        return false;
    }

    return SecureProtocol_AddPeer(&context->protocol,
                                  peerDeviceId,
                                  peerKey) == SECURE_PROTOCOL_OK;
}

SecureTransportStatus_t SecureCommunication_BeginMessage(
    SecureCommunication_t *context,
    uint8_t destinationId,
    SecureMessageType_t messageType,
    const uint8_t *message,
    uint16_t messageLength,
    SecureCommunicationTxState_t *state)
{
    if (context == NULL || !context->initialized)
    {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    return SecureTransport_BeginMessage(&context->transport,
                                        destinationId,
                                        messageType,
                                        message,
                                        messageLength,
                                        state);
}

SecureTransportStatus_t SecureCommunication_CreateNextFrame(
    SecureCommunication_t *context,
    SecureCommunicationTxState_t *state,
    uint8_t *frame,
    uint8_t frameBufferSize,
    uint8_t *frameLength,
    bool *messageComplete)
{
    if (context == NULL || !context->initialized)
    {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    return SecureTransport_CreateNextFrame(&context->transport,
                                           state,
                                           frame,
                                           frameBufferSize,
                                           frameLength,
                                           messageComplete);
}

SecureTransportStatus_t SecureCommunication_ProcessFrame(
    SecureCommunication_t *context,
    const uint8_t *frame,
    uint8_t frameLength,
    SecureMessageType_t *messageType,
    uint8_t *message,
    uint16_t messageBufferSize,
    uint16_t *messageLength,
    uint8_t *sourceDeviceId)
{
    if (context == NULL || !context->initialized)
    {
        return SECURE_TRANSPORT_INVALID_ARGUMENT;
    }

    return SecureTransport_ProcessFrame(&context->transport,
                                        frame,
                                        frameLength,
                                        messageType,
                                        message,
                                        messageBufferSize,
                                        messageLength,
                                        sourceDeviceId);
}

SecureProtocolStatus_t SecureCommunication_GetLastProtocolStatus(
    const SecureCommunication_t *context)
{
    if (context == NULL)
    {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }

    return SecureTransport_GetLastProtocolStatus(&context->transport);
}

bool SecureCommunication_IsInitialized(const SecureCommunication_t *context)
{
    return context != NULL && context->initialized;
}
