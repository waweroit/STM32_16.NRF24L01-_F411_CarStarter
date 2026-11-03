#ifndef COMMUNICATION_LINK_H
#define COMMUNICATION_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMMUNICATION_PHYSICAL_ADDRESS_MAX_SIZE 8u

typedef struct
{
    uint8_t length;
    uint8_t bytes[COMMUNICATION_PHYSICAL_ADDRESS_MAX_SIZE];
} CommunicationPhysicalAddress_t;

typedef enum
{
    COMMUNICATION_LINK_OK = 0,
    COMMUNICATION_LINK_TIMEOUT,
    COMMUNICATION_LINK_MAX_RETRY,
    COMMUNICATION_LINK_INVALID_ARGUMENT,
    COMMUNICATION_LINK_ERROR
} CommunicationLinkStatus_t;

typedef CommunicationLinkStatus_t (*CommunicationLinkSendFrameFn)(
    void *context,
    const CommunicationPhysicalAddress_t *destination,
    const uint8_t *frame,
    uint8_t frameLength,
    uint32_t timeoutMs);

typedef CommunicationLinkStatus_t (*CommunicationLinkReceiveFrameFn)(
    void *context,
    uint8_t *frame,
    uint8_t frameBufferSize,
    uint8_t *frameLength);

typedef struct
{
    void *context;
    CommunicationLinkSendFrameFn SendFrame;
    CommunicationLinkReceiveFrameFn ReceiveFrame;
} CommunicationLink_t;

const char *CommunicationLink_StatusToString(CommunicationLinkStatus_t status);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_LINK_H */
