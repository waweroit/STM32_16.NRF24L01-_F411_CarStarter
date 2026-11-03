#ifndef SECURE_PROTOCOL_H
#define SECURE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SECURE_PROTOCOL_VERSION 1u
#define SECURE_MAX_PAYLOAD_SIZE 11u
#define SECURE_AUTH_TAG_SIZE 8u
#define SECURE_FRAME_HEADER_SIZE 13u
#define SECURE_FRAME_OVERHEAD (SECURE_FRAME_HEADER_SIZE + SECURE_AUTH_TAG_SIZE)
#define SECURE_MAX_FRAME_SIZE 32u
#define SECURE_MAX_PEERS 16u

/** @brief Authenticated application message types. */
typedef enum {
    MESSAGE_TYPE_NONE = 0x00,
    MESSAGE_TYPE_ACK_APPLICATION = 0x01,
    MESSAGE_TYPE_TEMPERATURE = 0x02,
    MESSAGE_TYPE_PRESSURE = 0x03,
    MESSAGE_TYPE_HUMIDITY = 0x04,
	MESSAGE_TYPE_LIGHT_MESURMENT =0x05,
	MESSAGE_TYPE_TEMP_PRES_HUMID_MESURMENT =0x06,
    /** Reserved internal type used by SecureTransport fragmentation. */
    MESSAGE_TYPE_FRAGMENT = 0x7Fu
} SecureMessageType_t;

/** @brief Secure protocol operation result. */
typedef enum {
    SECURE_PROTOCOL_OK = 0,
    SECURE_PROTOCOL_INVALID_ARGUMENT,
    SECURE_PROTOCOL_UNKNOWN_DEVICE,
    SECURE_PROTOCOL_WRONG_DESTINATION,
    SECURE_PROTOCOL_INVALID_VERSION,
    SECURE_PROTOCOL_INVALID_LENGTH,
    SECURE_PROTOCOL_AUTHENTICATION_FAILED,
    SECURE_PROTOCOL_REPLAY_DETECTED,
    SECURE_PROTOCOL_SESSION_REJECTED,
    SECURE_PROTOCOL_CRYPTO_ERROR,
    SECURE_PROTOCOL_BUFFER_TOO_SMALL,
    SECURE_PROTOCOL_COUNTER_EXHAUSTED,
    SECURE_PROTOCOL_PEER_LIMIT_REACHED,
    SECURE_PROTOCOL_STORAGE_ERROR
} SecureProtocolStatus_t;

/**
 * @brief Optional persistent storage port.
 *
 * Boot-counter callbacks are required by the device examples. Peer-session
 * callbacks are optional but strongly recommended for replay protection across
 * receiver resets. Implementations must provide atomic/wear-managed records.
 */
typedef struct {
    bool (*LoadBootCounter)(uint32_t *counter);
    bool (*SaveBootCounter)(uint32_t counter);
    bool (*LoadPeerSession)(uint8_t peerId, uint32_t *sessionId, uint32_t *lastCounter);
    bool (*SavePeerSession)(uint8_t peerId, uint32_t sessionId, uint32_t lastCounter);
} SecureStorageInterface_t;

/** @brief Mutable counters and authorized sender key for one remote device. */
typedef struct {
    uint8_t remoteDeviceId;
    uint8_t peerMasterKey[CRYPTO_AES_KEY_SIZE];
    uint32_t txCounter;
    uint32_t lastAcceptedRxCounter;
    uint32_t acceptedRxSessionId;
    bool rxCounterInitialized;
    bool inUse;
} SecurePeerContext_t;

/** @brief Protocol instance. One task must own it or the caller must serialize access. */
typedef struct {
    uint8_t localDeviceId;
    uint32_t localSessionId;
    uint8_t localMasterKey[CRYPTO_AES_KEY_SIZE];
    SecurePeerContext_t peers[SECURE_MAX_PEERS];
} SecureProtocolContext_t;

/** @brief Initialize an instance with a nonzero local ID/session and local sender master key. */
SecureProtocolStatus_t SecureProtocol_Init(SecureProtocolContext_t *context,
                                           uint8_t localDeviceId,
                                           uint32_t localSessionId,
                                           const uint8_t localMasterKey[CRYPTO_AES_KEY_SIZE]);
/** @brief Authorize one peer and store its sender master key. */
SecureProtocolStatus_t SecureProtocol_AddPeer(SecureProtocolContext_t *context,
                                              uint8_t peerDeviceId,
                                              const uint8_t peerMasterKey[CRYPTO_AES_KEY_SIZE]);
/** @brief Restore persistent replay state for an already-added peer. */
SecureProtocolStatus_t SecureProtocol_SeedPeerRxState(SecureProtocolContext_t *context,
                                                      uint8_t peerDeviceId,
                                                      uint32_t sessionId,
                                                      uint32_t lastCounter,
                                                      bool initialized);
/** @brief Encrypt, authenticate and serialize one application message. */
SecureProtocolStatus_t SecureProtocol_CreateFrame(SecureProtocolContext_t *context,
                                                  uint8_t destinationId,
                                                  SecureMessageType_t messageType,
                                                  const uint8_t *plainPayload,
                                                  uint8_t plainPayloadLength,
                                                  uint8_t *serializedFrame,
                                                  uint8_t serializedFrameBufferSize,
                                                  uint8_t *serializedFrameLength);
/** @brief Validate, authenticate, anti-replay check and decrypt one frame. */
SecureProtocolStatus_t SecureProtocol_ProcessFrame(SecureProtocolContext_t *context,
                                                   const uint8_t *serializedFrame,
                                                   uint8_t serializedFrameLength,
                                                   SecureMessageType_t *messageType,
                                                   uint8_t *plainPayload,
                                                   uint8_t plainPayloadBufferSize,
                                                   uint8_t *plainPayloadLength,
                                                   uint8_t *sourceDeviceId);
/** @brief Serialize fields explicitly in big-endian order. */
SecureProtocolStatus_t SecureProtocol_SerializeFrame(uint8_t version, uint8_t sourceId,
                                                     uint8_t destinationId, uint8_t messageType,
                                                     uint32_t sessionId, uint32_t counter,
                                                     const uint8_t *encryptedPayload,
                                                     uint8_t payloadLength,
                                                     const uint8_t tag[SECURE_AUTH_TAG_SIZE],
                                                     uint8_t *output, uint8_t outputSize,
                                                     uint8_t *outputLength);
/** @brief Parse and validate frame layout without copying ciphertext/tag. */
SecureProtocolStatus_t SecureProtocol_DeserializeFrame(const uint8_t *frame, uint8_t frameLength,
                                                       uint8_t *version, uint8_t *sourceId,
                                                       uint8_t *destinationId, uint8_t *messageType,
                                                       uint32_t *sessionId, uint32_t *counter,
                                                       const uint8_t **encryptedPayload,
                                                       uint8_t *payloadLength,
                                                       const uint8_t **tag);
/** @brief Construct the 16-byte AES-CTR initial counter block. */
void SecureProtocol_BuildNonce(uint8_t version, uint8_t sourceId, uint8_t destinationId,
                               uint8_t direction, uint32_t sessionId, uint32_t counter,
                               uint8_t nonce[CRYPTO_AES_BLOCK_SIZE]);
/** @brief Generate a boot-generation-based session ID for STM32 without hardware RNG. */
SecureProtocolStatus_t SecureProtocol_GenerateSessionId(const uint8_t masterKey[CRYPTO_AES_KEY_SIZE],
                                                        const uint32_t uniqueDeviceId[3],
                                                        uint32_t bootCounter,
                                                        uint32_t timerValue,
                                                        uint16_t adcEntropy,
                                                        uint32_t *sessionId);
/** @brief Extract the monotonic 24-bit boot generation from a session ID. */
uint32_t SecureProtocol_GetSessionGeneration(uint32_t sessionId);
/** @brief Convert status to a static diagnostic string. */
const char *SecureProtocol_StatusToString(SecureProtocolStatus_t status);

#ifdef __cplusplus
}
#endif
#endif
