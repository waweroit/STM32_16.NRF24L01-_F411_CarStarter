#include "NrfLink.h"

#include <string.h>

static CommunicationLinkStatus_t NrfLink_AdapterSend(
    void *context,
    const CommunicationPhysicalAddress_t *destination,
    const uint8_t *frame,
    uint8_t frameLength,
    uint32_t timeoutMs);

static CommunicationLinkStatus_t NrfLink_AdapterReceive(
    void *context,
    uint8_t *frame,
    uint8_t frameBufferSize,
    uint8_t *frameLength);

static CommunicationLinkStatus_t MapStatus(NRF24_Status_t status);

NRF24_Status_t NrfLink_Init(NrfLink_t *link,
                            SPI_HandleTypeDef *hspi,
                            GPIO_TypeDef *cePort,
                            uint16_t cePin,
                            GPIO_TypeDef *csnPort,
                            uint16_t csnPin,
                            const uint8_t localAddress[NRF_LINK_ADDRESS_SIZE])
{
    NRF24_Status_t status;

    if (link == NULL || hspi == NULL || cePort == NULL || csnPort == NULL ||
        localAddress == NULL)
    {
        return NRF24_INVALID_ARGUMENT;
    }

    memset(link, 0, sizeof(*link));
    memcpy(link->localAddress, localAddress, NRF_LINK_ADDRESS_SIZE);

    status = NRF24_Init(&link->radio,
                        hspi,
                        cePort,
                        cePin,
                        csnPort,
                        csnPin);
    if (status != NRF24_OK)
    {
        return status;
    }

    status = NRF24_SetRxAddress(&link->radio, 0u, link->localAddress);
    if (status != NRF24_OK)
    {
        return status;
    }

    return NRF24_StartListening(&link->radio);
}

NRF24_Status_t NrfLink_SendFrame(NrfLink_t *link,
                                 const uint8_t destinationAddress[NRF_LINK_ADDRESS_SIZE],
                                 const uint8_t *frame,
                                 uint8_t frameLength,
                                 uint32_t timeoutMs)
{
    NRF24_Status_t status;
    NRF24_Status_t restoreStatus;

    if (link == NULL || destinationAddress == NULL || frame == NULL ||
        frameLength == 0u || frameLength > NRF24_MAX_PAYLOAD_SIZE)
    {
        return NRF24_INVALID_ARGUMENT;
    }

    status = NRF24_StopListening(&link->radio);
    if (status == NRF24_OK)
    {
        status = NRF24_SetTxAddress(&link->radio, destinationAddress);
    }
    if (status == NRF24_OK)
    {
        /* nRF24 Auto ACK requires pipe 0 to match TX_ADDR while transmitting. */
        status = NRF24_SetRxAddress(&link->radio, 0u, destinationAddress);
    }
    if (status == NRF24_OK)
    {
        status = NRF24_Send(&link->radio, frame, frameLength, timeoutMs);
    }

    /* Always return the radio to its own physical RX address. */
    restoreStatus = NRF24_SetRxAddress(&link->radio, 0u, link->localAddress);
    if (restoreStatus == NRF24_OK)
    {
        restoreStatus = NRF24_StartListening(&link->radio);
    }

    if (status != NRF24_OK)
    {
        return status;
    }

    return restoreStatus;
}

NRF24_Status_t NrfLink_ReceiveFrame(NrfLink_t *link,
                                    uint8_t *frame,
                                    uint8_t frameBufferSize,
                                    uint8_t *frameLength)
{
    if (link == NULL)
    {
        return NRF24_INVALID_ARGUMENT;
    }

    return NRF24_Receive(&link->radio,
                         frame,
                         frameBufferSize,
                         frameLength);
}

CommunicationLink_t NrfLink_AsCommunicationLink(NrfLink_t *link)
{
    CommunicationLink_t interface = {0};

    interface.context = link;
    interface.SendFrame = NrfLink_AdapterSend;
    interface.ReceiveFrame = NrfLink_AdapterReceive;
    return interface;
}

static CommunicationLinkStatus_t NrfLink_AdapterSend(
    void *context,
    const CommunicationPhysicalAddress_t *destination,
    const uint8_t *frame,
    uint8_t frameLength,
    uint32_t timeoutMs)
{
    if (context == NULL || destination == NULL ||
        destination->length != NRF_LINK_ADDRESS_SIZE)
    {
        return COMMUNICATION_LINK_INVALID_ARGUMENT;
    }

    return MapStatus(NrfLink_SendFrame((NrfLink_t *)context,
                                       destination->bytes,
                                       frame,
                                       frameLength,
                                       timeoutMs));
}

static CommunicationLinkStatus_t NrfLink_AdapterReceive(
    void *context,
    uint8_t *frame,
    uint8_t frameBufferSize,
    uint8_t *frameLength)
{
    if (context == NULL)
    {
        return COMMUNICATION_LINK_INVALID_ARGUMENT;
    }

    return MapStatus(NrfLink_ReceiveFrame((NrfLink_t *)context,
                                          frame,
                                          frameBufferSize,
                                          frameLength));
}

static CommunicationLinkStatus_t MapStatus(NRF24_Status_t status)
{
    switch (status)
    {
    case NRF24_OK: return COMMUNICATION_LINK_OK;
    case NRF24_TIMEOUT: return COMMUNICATION_LINK_TIMEOUT;
    case NRF24_MAX_RETRY: return COMMUNICATION_LINK_MAX_RETRY;
    case NRF24_INVALID_ARGUMENT:
    case NRF24_INVALID_PAYLOAD_WIDTH:
        return COMMUNICATION_LINK_INVALID_ARGUMENT;
    case NRF24_ERROR:
    default:
        return COMMUNICATION_LINK_ERROR;
    }
}
