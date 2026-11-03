#include "SecureProtocol.h"
#include "DeviceKeys.h"
#include <string.h>

#define NONCE_DIRECTION_DATA 0u

#if (SECURE_FRAME_OVERHEAD + SECURE_MAX_PAYLOAD_SIZE) != SECURE_MAX_FRAME_SIZE
#error "Secure frame constants must total exactly 32 bytes"
#endif

static void put_u32_be(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

static uint32_t get_u32_be(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0] << 24) |
           ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) |
           (uint32_t)buffer[3];
}

static SecurePeerContext_t *find_peer(SecureProtocolContext_t *context,
                                      uint8_t peerDeviceId)
{
    size_t i;
    for (i = 0u; i < SECURE_MAX_PEERS; ++i) {
        if (context->peers[i].inUse &&
            context->peers[i].remoteDeviceId == peerDeviceId) {
            return &context->peers[i];
        }
    }
    return NULL;
}

static SecureProtocolStatus_t derive_directional_keys(const uint8_t masterKey[16],
                                                      uint8_t sourceId,
                                                      uint8_t destinationId,
                                                      uint8_t encryptionKey[16],
                                                      uint8_t authenticationKey[16])
{
    CryptoStatus_t status;

    status = DeviceKeys_DeriveConnectionKey(masterKey, "ENC", sourceId,
                                            destinationId, encryptionKey);
    if (status != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    status = DeviceKeys_DeriveConnectionKey(masterKey, "AUTH", sourceId,
                                            destinationId, authenticationKey);
    return status == CRYPTO_OK ? SECURE_PROTOCOL_OK : SECURE_PROTOCOL_CRYPTO_ERROR;
}

SecureProtocolStatus_t SecureProtocol_Init(SecureProtocolContext_t *context,
                                           uint8_t localDeviceId,
                                           uint32_t localSessionId,
                                           const uint8_t localMasterKey[16])
{
    if (context == NULL || localMasterKey == NULL ||
        localDeviceId == 0u || localSessionId == 0u) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(*context));
    context->localDeviceId = localDeviceId;
    context->localSessionId = localSessionId;
    memcpy(context->localMasterKey, localMasterKey, CRYPTO_AES_KEY_SIZE);
    return SECURE_PROTOCOL_OK;
}

SecureProtocolStatus_t SecureProtocol_AddPeer(SecureProtocolContext_t *context,
                                              uint8_t peerDeviceId,
                                              const uint8_t peerMasterKey[16])
{
    size_t i;

    if (context == NULL || peerMasterKey == NULL || peerDeviceId == 0u ||
        peerDeviceId == context->localDeviceId) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (find_peer(context, peerDeviceId) != NULL) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }

    for (i = 0u; i < SECURE_MAX_PEERS; ++i) {
        SecurePeerContext_t *peer = &context->peers[i];
        if (!peer->inUse) {
            memset(peer, 0, sizeof(*peer));
            peer->inUse = true;
            peer->remoteDeviceId = peerDeviceId;
            memcpy(peer->peerMasterKey, peerMasterKey, CRYPTO_AES_KEY_SIZE);
            return SECURE_PROTOCOL_OK;
        }
    }
    return SECURE_PROTOCOL_PEER_LIMIT_REACHED;
}

SecureProtocolStatus_t SecureProtocol_SeedPeerRxState(SecureProtocolContext_t *context,
                                                      uint8_t peerDeviceId,
                                                      uint32_t sessionId,
                                                      uint32_t lastCounter,
                                                      bool initialized)
{
    SecurePeerContext_t *peer;
    if (context == NULL) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }
    peer = find_peer(context, peerDeviceId);
    if (peer == NULL) {
        return SECURE_PROTOCOL_UNKNOWN_DEVICE;
    }
    peer->acceptedRxSessionId = sessionId;
    peer->lastAcceptedRxCounter = lastCounter;
    peer->rxCounterInitialized = initialized;
    return SECURE_PROTOCOL_OK;
}

void SecureProtocol_BuildNonce(uint8_t version,
                               uint8_t sourceId,
                               uint8_t destinationId,
                               uint8_t direction,
                               uint32_t sessionId,
                               uint32_t counter,
                               uint8_t nonce[16])
{
    if (nonce == NULL) {
        return;
    }
    nonce[0] = version;
    nonce[1] = sourceId;
    nonce[2] = destinationId;
    nonce[3] = direction;
    put_u32_be(&nonce[4], sessionId);
    put_u32_be(&nonce[8], counter);
    memset(&nonce[12], 0, 4u);
}

SecureProtocolStatus_t SecureProtocol_SerializeFrame(uint8_t version,
                                                     uint8_t sourceId,
                                                     uint8_t destinationId,
                                                     uint8_t messageType,
                                                     uint32_t sessionId,
                                                     uint32_t counter,
                                                     const uint8_t *encryptedPayload,
                                                     uint8_t payloadLength,
                                                     const uint8_t tag[8],
                                                     uint8_t *output,
                                                     uint8_t outputSize,
                                                     uint8_t *outputLength)
{
    uint8_t requiredLength = (uint8_t)(SECURE_FRAME_OVERHEAD + payloadLength);

    if (output == NULL || outputLength == NULL || tag == NULL ||
        (payloadLength > 0u && encryptedPayload == NULL)) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payloadLength > SECURE_MAX_PAYLOAD_SIZE) {
        return SECURE_PROTOCOL_INVALID_LENGTH;
    }
    if (outputSize < requiredLength) {
        return SECURE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    output[0] = version;
    output[1] = sourceId;
    output[2] = destinationId;
    output[3] = messageType;
    put_u32_be(&output[4], sessionId);
    put_u32_be(&output[8], counter);
    output[12] = payloadLength;
    if (payloadLength > 0u) {
        memcpy(&output[SECURE_FRAME_HEADER_SIZE], encryptedPayload, payloadLength);
    }
    memcpy(&output[SECURE_FRAME_HEADER_SIZE + payloadLength], tag,
           SECURE_AUTH_TAG_SIZE);
    *outputLength = requiredLength;
    return SECURE_PROTOCOL_OK;
}

SecureProtocolStatus_t SecureProtocol_DeserializeFrame(const uint8_t *frame,
                                                       uint8_t frameLength,
                                                       uint8_t *version,
                                                       uint8_t *sourceId,
                                                       uint8_t *destinationId,
                                                       uint8_t *messageType,
                                                       uint32_t *sessionId,
                                                       uint32_t *counter,
                                                       const uint8_t **encryptedPayload,
                                                       uint8_t *payloadLength,
                                                       const uint8_t **tag)
{
    uint8_t length;

    if (frame == NULL || version == NULL || sourceId == NULL ||
        destinationId == NULL || messageType == NULL || sessionId == NULL ||
        counter == NULL || encryptedPayload == NULL || payloadLength == NULL ||
        tag == NULL) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (frameLength < SECURE_FRAME_OVERHEAD ||
        frameLength > SECURE_MAX_FRAME_SIZE) {
        return SECURE_PROTOCOL_INVALID_LENGTH;
    }

    length = frame[12];
    if (length > SECURE_MAX_PAYLOAD_SIZE ||
        (uint8_t)(SECURE_FRAME_OVERHEAD + length) != frameLength) {
        return SECURE_PROTOCOL_INVALID_LENGTH;
    }

    *version = frame[0];
    *sourceId = frame[1];
    *destinationId = frame[2];
    *messageType = frame[3];
    *sessionId = get_u32_be(&frame[4]);
    *counter = get_u32_be(&frame[8]);
    *payloadLength = length;
    *encryptedPayload = &frame[SECURE_FRAME_HEADER_SIZE];
    *tag = &frame[SECURE_FRAME_HEADER_SIZE + length];
    return SECURE_PROTOCOL_OK;
}

SecureProtocolStatus_t SecureProtocol_CreateFrame(SecureProtocolContext_t *context,
                                                  uint8_t destinationId,
                                                  SecureMessageType_t messageType,
                                                  const uint8_t *plainPayload,
                                                  uint8_t plainPayloadLength,
                                                  uint8_t *serializedFrame,
                                                  uint8_t serializedFrameBufferSize,
                                                  uint8_t *serializedFrameLength)
{
    SecurePeerContext_t *peer;
    uint8_t encryptionKey[16];
    uint8_t authenticationKey[16];
    uint8_t nonce[16];
    uint8_t encryptedPayload[SECURE_MAX_PAYLOAD_SIZE];
    uint8_t tag[SECURE_AUTH_TAG_SIZE];
    uint8_t authenticatedData[SECURE_FRAME_HEADER_SIZE + SECURE_MAX_PAYLOAD_SIZE];
    uint32_t counter;
    CryptoStatus_t cryptoStatus;
    SecureProtocolStatus_t status;

    if (context == NULL || serializedFrame == NULL ||
        serializedFrameLength == NULL ||
        (plainPayloadLength > 0u && plainPayload == NULL)) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (plainPayloadLength > SECURE_MAX_PAYLOAD_SIZE) {
        return SECURE_PROTOCOL_INVALID_LENGTH;
    }

    peer = find_peer(context, destinationId);
    if (peer == NULL) {
        return SECURE_PROTOCOL_UNKNOWN_DEVICE;
    }
    if (peer->txCounter == UINT32_MAX) {
        return SECURE_PROTOCOL_COUNTER_EXHAUSTED;
    }
    counter = peer->txCounter;

    status = derive_directional_keys(context->localMasterKey,
                                     context->localDeviceId,
                                     destinationId,
                                     encryptionKey,
                                     authenticationKey);
    if (status != SECURE_PROTOCOL_OK) {
        return status;
    }

    SecureProtocol_BuildNonce(SECURE_PROTOCOL_VERSION,
                              context->localDeviceId,
                              destinationId,
                              NONCE_DIRECTION_DATA,
                              context->localSessionId,
                              counter,
                              nonce);
    cryptoStatus = AES_CTR_Encrypt(encryptionKey, nonce, plainPayload,
                                   encryptedPayload, plainPayloadLength);
    if (cryptoStatus != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    authenticatedData[0] = SECURE_PROTOCOL_VERSION;
    authenticatedData[1] = context->localDeviceId;
    authenticatedData[2] = destinationId;
    authenticatedData[3] = (uint8_t)messageType;
    put_u32_be(&authenticatedData[4], context->localSessionId);
    put_u32_be(&authenticatedData[8], counter);
    authenticatedData[12] = plainPayloadLength;
    if (plainPayloadLength > 0u) {
        memcpy(&authenticatedData[SECURE_FRAME_HEADER_SIZE], encryptedPayload,
               plainPayloadLength);
    }

    cryptoStatus = Crypto_CalculateAuthTag(authenticationKey,
                                           authenticatedData,
                                           SECURE_FRAME_HEADER_SIZE + plainPayloadLength,
                                           tag);
    if (cryptoStatus != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    status = SecureProtocol_SerializeFrame(SECURE_PROTOCOL_VERSION,
                                           context->localDeviceId,
                                           destinationId,
                                           (uint8_t)messageType,
                                           context->localSessionId,
                                           counter,
                                           encryptedPayload,
                                           plainPayloadLength,
                                           tag,
                                           serializedFrame,
                                           serializedFrameBufferSize,
                                           serializedFrameLength);
    if (status == SECURE_PROTOCOL_OK) {
        /* Consume the nonce even if the later RF transmission fails. */
        peer->txCounter = counter + 1u;
    }
    return status;
}

SecureProtocolStatus_t SecureProtocol_ProcessFrame(SecureProtocolContext_t *context,
                                                   const uint8_t *serializedFrame,
                                                   uint8_t serializedFrameLength,
                                                   SecureMessageType_t *messageType,
                                                   uint8_t *plainPayload,
                                                   uint8_t plainPayloadBufferSize,
                                                   uint8_t *plainPayloadLength,
                                                   uint8_t *sourceDeviceId)
{
    uint8_t version;
    uint8_t sourceId;
    uint8_t destinationId;
    uint8_t rawMessageType;
    uint8_t payloadLength;
    uint8_t encryptionKey[16];
    uint8_t authenticationKey[16];
    uint8_t nonce[16];
    uint32_t sessionId;
    uint32_t counter;
    uint32_t incomingGeneration;
    uint32_t currentGeneration;
    const uint8_t *encryptedPayload;
    const uint8_t *tag;
    SecurePeerContext_t *peer;
    SecureProtocolStatus_t status;
    CryptoStatus_t cryptoStatus;

    if (context == NULL || serializedFrame == NULL || messageType == NULL ||
        plainPayloadLength == NULL || sourceDeviceId == NULL) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }

    status = SecureProtocol_DeserializeFrame(serializedFrame,
                                             serializedFrameLength,
                                             &version,
                                             &sourceId,
                                             &destinationId,
                                             &rawMessageType,
                                             &sessionId,
                                             &counter,
                                             &encryptedPayload,
                                             &payloadLength,
                                             &tag);
    if (status != SECURE_PROTOCOL_OK) {
        return status;
    }
    if (version != SECURE_PROTOCOL_VERSION) {
        return SECURE_PROTOCOL_INVALID_VERSION;
    }
    if (destinationId != context->localDeviceId) {
        return SECURE_PROTOCOL_WRONG_DESTINATION;
    }

    peer = find_peer(context, sourceId);
    if (peer == NULL) {
        return SECURE_PROTOCOL_UNKNOWN_DEVICE;
    }
    if (payloadLength > plainPayloadBufferSize ||
        (payloadLength > 0u && plainPayload == NULL)) {
        return SECURE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    incomingGeneration = SecureProtocol_GetSessionGeneration(sessionId);
    currentGeneration = SecureProtocol_GetSessionGeneration(peer->acceptedRxSessionId);
    if (peer->rxCounterInitialized) {
        if (incomingGeneration < currentGeneration) {
            return SECURE_PROTOCOL_SESSION_REJECTED;
        }
        if (incomingGeneration == currentGeneration &&
            sessionId != peer->acceptedRxSessionId) {
            return SECURE_PROTOCOL_SESSION_REJECTED;
        }
        if (sessionId == peer->acceptedRxSessionId &&
            counter <= peer->lastAcceptedRxCounter) {
            return SECURE_PROTOCOL_REPLAY_DETECTED;
        }
    }

    status = derive_directional_keys(peer->peerMasterKey,
                                     sourceId,
                                     context->localDeviceId,
                                     encryptionKey,
                                     authenticationKey);
    if (status != SECURE_PROTOCOL_OK) {
        return status;
    }

    cryptoStatus = Crypto_VerifyAuthTag(authenticationKey,
                                        serializedFrame,
                                        SECURE_FRAME_HEADER_SIZE + payloadLength,
                                        tag);
    if (cryptoStatus == CRYPTO_AUTHENTICATION_FAILED) {
        return SECURE_PROTOCOL_AUTHENTICATION_FAILED;
    }
    if (cryptoStatus != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    SecureProtocol_BuildNonce(version,
                              sourceId,
                              destinationId,
                              NONCE_DIRECTION_DATA,
                              sessionId,
                              counter,
                              nonce);
    cryptoStatus = AES_CTR_Decrypt(encryptionKey, nonce, encryptedPayload,
                                   plainPayload, payloadLength);
    if (cryptoStatus != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    /* Update anti-replay state only after authentication and decryption succeed. */
    peer->acceptedRxSessionId = sessionId;
    peer->lastAcceptedRxCounter = counter;
    peer->rxCounterInitialized = true;
    *messageType = (SecureMessageType_t)rawMessageType;
    *plainPayloadLength = payloadLength;
    *sourceDeviceId = sourceId;
    return SECURE_PROTOCOL_OK;
}

SecureProtocolStatus_t SecureProtocol_GenerateSessionId(const uint8_t masterKey[16],
                                                        const uint32_t uniqueDeviceId[3],
                                                        uint32_t bootCounter,
                                                        uint32_t timerValue,
                                                        uint16_t adcEntropy,
                                                        uint32_t *sessionId)
{
    uint8_t input[22];
    uint8_t mac[16];
    CryptoStatus_t status;

    if (masterKey == NULL || uniqueDeviceId == NULL || sessionId == NULL ||
        bootCounter == 0u || bootCounter > 0x00FFFFFFu) {
        return SECURE_PROTOCOL_INVALID_ARGUMENT;
    }

    put_u32_be(&input[0], uniqueDeviceId[0]);
    put_u32_be(&input[4], uniqueDeviceId[1]);
    put_u32_be(&input[8], uniqueDeviceId[2]);
    put_u32_be(&input[12], bootCounter);
    put_u32_be(&input[16], timerValue);
    input[20] = (uint8_t)(adcEntropy >> 8);
    input[21] = (uint8_t)adcEntropy;

    status = Crypto_CalculateCmac(masterKey, input, sizeof(input), mac);
    if (status != CRYPTO_OK) {
        return SECURE_PROTOCOL_CRYPTO_ERROR;
    }

    *sessionId = (bootCounter << 8) | mac[0];
    return SECURE_PROTOCOL_OK;
}

uint32_t SecureProtocol_GetSessionGeneration(uint32_t sessionId)
{
    return sessionId >> 8;
}

const char *SecureProtocol_StatusToString(SecureProtocolStatus_t status)
{
    switch (status) {
    case SECURE_PROTOCOL_OK: return "OK";
    case SECURE_PROTOCOL_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case SECURE_PROTOCOL_UNKNOWN_DEVICE: return "UNKNOWN_DEVICE";
    case SECURE_PROTOCOL_WRONG_DESTINATION: return "WRONG_DESTINATION";
    case SECURE_PROTOCOL_INVALID_VERSION: return "INVALID_VERSION";
    case SECURE_PROTOCOL_INVALID_LENGTH: return "INVALID_LENGTH";
    case SECURE_PROTOCOL_AUTHENTICATION_FAILED: return "AUTHENTICATION_FAILED";
    case SECURE_PROTOCOL_REPLAY_DETECTED: return "REPLAY_DETECTED";
    case SECURE_PROTOCOL_SESSION_REJECTED: return "SESSION_REJECTED";
    case SECURE_PROTOCOL_CRYPTO_ERROR: return "CRYPTO_ERROR";
    case SECURE_PROTOCOL_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case SECURE_PROTOCOL_COUNTER_EXHAUSTED: return "COUNTER_EXHAUSTED";
    case SECURE_PROTOCOL_PEER_LIMIT_REACHED: return "PEER_LIMIT_REACHED";
    case SECURE_PROTOCOL_STORAGE_ERROR: return "STORAGE_ERROR";
    default: return "UNKNOWN";
    }
}
