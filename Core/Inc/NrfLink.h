#ifndef NRF_LINK_H
#define NRF_LINK_H

#include <stdint.h>
#include "nRF24L01.h"
#include "CommunicationLink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NRF_LINK_ADDRESS_SIZE 5u

typedef struct
{
    NRF24_Handle_t radio;
    uint8_t localAddress[NRF_LINK_ADDRESS_SIZE];
} NrfLink_t;

NRF24_Status_t NrfLink_Init(NrfLink_t *link,
                            SPI_HandleTypeDef *hspi,
                            GPIO_TypeDef *cePort,
                            uint16_t cePin,
                            GPIO_TypeDef *csnPort,
                            uint16_t csnPin,
                            const uint8_t localAddress[NRF_LINK_ADDRESS_SIZE]);

NRF24_Status_t NrfLink_SendFrame(NrfLink_t *link,
                                 const uint8_t destinationAddress[NRF_LINK_ADDRESS_SIZE],
                                 const uint8_t *frame,
                                 uint8_t frameLength,
                                 uint32_t timeoutMs);

NRF24_Status_t NrfLink_ReceiveFrame(NrfLink_t *link,
                                    uint8_t *frame,
                                    uint8_t frameBufferSize,
                                    uint8_t *frameLength);

CommunicationLink_t NrfLink_AsCommunicationLink(NrfLink_t *link);

#ifdef __cplusplus
}
#endif

#endif /* NRF_LINK_H */
