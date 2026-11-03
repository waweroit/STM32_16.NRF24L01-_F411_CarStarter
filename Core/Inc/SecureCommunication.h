#ifndef SECURE_COMMUNICATION_H
#define SECURE_COMMUNICATION_H

#include <stdbool.h>
#include <stdint.h>
#include "SecureTransport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef SecureTransportTxState_t SecureCommunicationTxState_t;

typedef struct
{
    SecureProtocolContext_t protocol;
    SecureTransportContext_t transport;
    uint8_t localDeviceId;
    uint32_t localSessionId;
    bool initialized;
} SecureCommunication_t;

bool SecureCommunication_Init(SecureCommunication_t *context,
                              uint8_t localDeviceId,
                              const uint8_t deviceKey[CRYPTO_AES_KEY_SIZE]);

bool SecureCommunication_AuthorizePeer(
    SecureCommunication_t *context,
    uint8_t peerDeviceId,
    const uint8_t peerKey[CRYPTO_AES_KEY_SIZE]);

SecureTransportStatus_t SecureCommunication_BeginMessage(
    SecureCommunication_t *context,
    uint8_t destinationId,
    SecureMessageType_t messageType,
    const uint8_t *message,
    uint16_t messageLength,
    SecureCommunicationTxState_t *state);

SecureTransportStatus_t SecureCommunication_CreateNextFrame(
    SecureCommunication_t *context,
    SecureCommunicationTxState_t *state,
    uint8_t *frame,
    uint8_t frameBufferSize,
    uint8_t *frameLength,
    bool *messageComplete);

SecureTransportStatus_t SecureCommunication_ProcessFrame(
    SecureCommunication_t *context,
    const uint8_t *frame,
    uint8_t frameLength,
    SecureMessageType_t *messageType,
    uint8_t *message,
    uint16_t messageBufferSize,
    uint16_t *messageLength,
    uint8_t *sourceDeviceId);

SecureProtocolStatus_t SecureCommunication_GetLastProtocolStatus(
    const SecureCommunication_t *context);

bool SecureCommunication_IsInitialized(const SecureCommunication_t *context);

#ifdef __cplusplus
}
#endif

#endif /* SECURE_COMMUNICATION_H */
