#ifndef SECURE_TRANSPORT_H
#define SECURE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "SecureProtocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum application message accepted by the fragmentation layer. */
#define SECURE_TRANSPORT_MAX_MESSAGE_SIZE 256u
/** Fragment metadata carried inside the encrypted SecureProtocol payload. */
#define SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE 4u
/** Application bytes carried by one fragmented SecureProtocol frame. */
#define SECURE_TRANSPORT_FRAGMENT_DATA_SIZE \
    (SECURE_MAX_PAYLOAD_SIZE - SECURE_TRANSPORT_FRAGMENT_HEADER_SIZE)
/** Maximum fragment count for one application message. */
#define SECURE_TRANSPORT_MAX_FRAGMENTS \
    ((SECURE_TRANSPORT_MAX_MESSAGE_SIZE + SECURE_TRANSPORT_FRAGMENT_DATA_SIZE - 1u) / \
     SECURE_TRANSPORT_FRAGMENT_DATA_SIZE)

/** Result of the application-message transport/fragmentation layer. */
typedef enum {
    SECURE_TRANSPORT_OK = 0,
    SECURE_TRANSPORT_IN_PROGRESS,
    SECURE_TRANSPORT_INVALID_ARGUMENT,
    SECURE_TRANSPORT_MESSAGE_TOO_LARGE,
    SECURE_TRANSPORT_INVALID_FRAGMENT,
    SECURE_TRANSPORT_BUFFER_TOO_SMALL,
    SECURE_TRANSPORT_PROTOCOL_ERROR,
    SECURE_TRANSPORT_REASSEMBLY_FULL,
    SECURE_TRANSPORT_NO_ACTIVE_TX
} SecureTransportStatus_t;

/** Incremental TX state. Valid only while one message is synchronously emitted. */
typedef struct {
    bool active;
    bool fragmented;
    uint8_t destinationId;
    SecureMessageType_t messageType;
    uint8_t messageId;
    const uint8_t *message;
    uint16_t messageLength;
    uint8_t fragmentCount;
    uint8_t nextFragmentIndex;
} SecureTransportTxState_t;

/** Reassembly state for one remote device. */
typedef struct {
    bool inUse;
    uint8_t sourceId;
    uint8_t messageId;
    SecureMessageType_t messageType;
    uint8_t fragmentCount;
    uint8_t lastFragmentLength;
    uint64_t receivedBitmap;
    uint8_t data[SECURE_TRANSPORT_MAX_MESSAGE_SIZE];
} SecureTransportReassembly_t;

/**
 * @brief Transport instance layered over one SecureProtocol instance.
 *
 * The object is stateful and must be owned by one task or externally serialized.
 * No dynamic allocation is used. One incomplete fragmented message is retained
 * per authorized SecureProtocol peer.
 */
typedef struct {
    SecureProtocolContext_t *protocol;
    uint8_t nextMessageId;
    SecureProtocolStatus_t lastProtocolStatus;
    SecureTransportReassembly_t reassembly[SECURE_MAX_PEERS];
} SecureTransportContext_t;

/** Initialize the fragmentation/reassembly layer. */
SecureTransportStatus_t SecureTransport_Init(SecureTransportContext_t *context,
                                              SecureProtocolContext_t *protocol);

/**
 * Start emitting one logical application message.
 * Messages <= SECURE_MAX_PAYLOAD_SIZE are sent as a single normal SecureProtocol
 * frame. Longer messages are transparently fragmented.
 */
SecureTransportStatus_t SecureTransport_BeginMessage(SecureTransportContext_t *context,
                                                      uint8_t destinationId,
                                                      SecureMessageType_t messageType,
                                                      const uint8_t *message,
                                                      uint16_t messageLength,
                                                      SecureTransportTxState_t *state);

/**
 * Build the next RF-sized SecureProtocol frame for an active TX message.
 * @param messageComplete Set true when this frame is the final frame.
 */
SecureTransportStatus_t SecureTransport_CreateNextFrame(SecureTransportContext_t *context,
                                                         SecureTransportTxState_t *state,
                                                         uint8_t *serializedFrame,
                                                         uint8_t serializedFrameBufferSize,
                                                         uint8_t *serializedFrameLength,
                                                         bool *messageComplete);

/**
 * Process one received SecureProtocol frame and reassemble fragmented messages.
 * Returns SECURE_TRANSPORT_IN_PROGRESS when a valid fragment was accepted but
 * the complete application message is not available yet.
 */
SecureTransportStatus_t SecureTransport_ProcessFrame(SecureTransportContext_t *context,
                                                      const uint8_t *serializedFrame,
                                                      uint8_t serializedFrameLength,
                                                      SecureMessageType_t *messageType,
                                                      uint8_t *message,
                                                      uint16_t messageBufferSize,
                                                      uint16_t *messageLength,
                                                      uint8_t *sourceDeviceId);

/** Return the most recent underlying SecureProtocol status. */
SecureProtocolStatus_t SecureTransport_GetLastProtocolStatus(
    const SecureTransportContext_t *context);

/** Drop an incomplete reassembly for one peer, if present. */
void SecureTransport_ResetPeerReassembly(SecureTransportContext_t *context,
                                         uint8_t sourceDeviceId);

/** Convert transport status to a static diagnostic string. */
const char *SecureTransport_StatusToString(SecureTransportStatus_t status);

#ifdef __cplusplus
}
#endif
#endif
