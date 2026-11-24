#include "NrfLink.h"
#include "main.h"
#include <string.h>

/* SPI register model for cold starts, with optional failure to latch PWR_UP. */
static uint8_t regs[32][5], command, offset;
static bool selected, listening, losePowerUp;
static bool loseDynpd;
static bool failFifo;
static bool txLoaded, txAttempted, failRestore;
static HAL_StatusTypeDef restoreFailure;
static uint8_t txOutcome;
static SPI_HandleTypeDef spi;

void RadioTestPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port==SPI1_NRF24_CS_GPIO_Port && pin==SPI1_NRF24_CS_Pin) {
        selected=state==GPIO_PIN_RESET;
        offset=0;
    }
    if (port==NRF24_CE_GPIO_Port && pin==NRF24_CE_Pin) {
        listening=state==GPIO_PIN_SET;
        if (listening && txLoaded && !(regs[0][0] & 1u)) {
            regs[7][0] |= txOutcome;
            txLoaded=false;
            txAttempted=true;
        }
    }
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, const uint8_t *tx,
                                          uint8_t *rx, uint16_t size, uint32_t timeout)
{
    (void)timeout;
    if (h!=&spi || !selected || size!=1 || offset>5) return HAL_ERROR;
    *rx=0;
    if (offset==0) {
        command=*tx;
        if (command==0x17u && failFifo) return HAL_TIMEOUT;
        if (command==0xa0u) txLoaded=true;
        /* Inject a bus failure while restoring RX_ADDR_P0 after TX. */
        if (command==0x2au && failRestore && txAttempted) {
            failRestore=false;
            return restoreFailure;
        }
    }
    else if (command<32) *rx=regs[command][offset-1];
    else if ((command & 0xe0u)==0x20u) {
        if (command==0x27u) regs[7][0] &= ~*tx;
        else if (loseDynpd && command==0x3cu) regs[28][offset-1]=0u;
        else regs[command & 31u][offset-1]=(losePowerUp && command==0x20u) ? *tx & ~2u : *tx;
    }
    ++offset;
    return HAL_OK;
}

int RadioTests(void)
{
    NrfLink_t link;
    const uint8_t address[]={0xc3,0xa2,0xa3,0xa4,0xa5};
    for (unsigned boot=0; boot<4; ++boot) {
        memset(regs,0,sizeof(regs));
        losePowerUp=boot==1;
        loseDynpd=boot==2;
        NRF24_Status_t status=NrfLink_Init(&link,&spi,NRF24_CE_GPIO_Port,NRF24_CE_Pin,
            SPI1_NRF24_CS_GPIO_Port,SPI1_NRF24_CS_Pin,address);
        if (losePowerUp || loseDynpd) {
            if (status==NRF24_OK || listening) return __LINE__;
            if (loseDynpd && link.radio.dynamicPayloadEnabled) return __LINE__;
        } else if (status!=NRF24_OK || !listening || selected || regs[0][0]!=0x0fu ||
                   regs[5][0]!=100 || memcmp(regs[10],address,sizeof(address))) return __LINE__;
    }
    const uint8_t destination[]={0xa1,0xb2,0xb3,0xb4,0xb5};
    const uint8_t frame[]={1,2,3};
    /* With permanently connected VCC, closing CE alone is insufficient for power-down. */
    for (unsigned wake=0u; wake<3u; ++wake) {
        if (NRF24_PowerDown(&link.radio)!=NRF24_OK || listening || (regs[0][0]&2u) ||
            memcmp(regs[10],address,sizeof(address))) return __LINE__;
        if (NRF24_StartListening(&link.radio)!=NRF24_OK || !listening || regs[0][0]!=0x0fu)
            return __LINE__;
        if (NRF24_PowerDown(&link.radio)!=NRF24_OK ||
            NrfLink_Init(&link,&spi,NRF24_CE_GPIO_Port,NRF24_CE_Pin,
                SPI1_NRF24_CS_GPIO_Port,SPI1_NRF24_CS_Pin,address)!=NRF24_OK ||
            !listening || regs[0][0]!=0x0fu || regs[28][0]!=1u || regs[29][0]!=4u ||
            memcmp(regs[10],address,sizeof(address))) return __LINE__;
    }
    /* A lost ACK must still leave the receiver listening at its own address. */
    for (unsigned outcome=0; outcome<2; ++outcome) {
        txOutcome=outcome ? 0x10u : 0x20u;
        NRF24_Status_t expected=outcome ? NRF24_MAX_RETRY : NRF24_OK;
        if (NrfLink_SendFrame(&link,destination,frame,sizeof(frame),100u)!=expected ||
            !listening || regs[0][0]!=0x0fu || selected ||
            memcmp(regs[10],address,sizeof(address))) return __LINE__;
        if (link.txFailureSnapshotValid != (outcome!=0u)) return __LINE__;
        if (outcome && (link.txFailureConfig!=0x0eu ||
            memcmp(link.txFailureAddress,destination,5u) ||
            memcmp(link.txFailureAckAddress,destination,5u))) return __LINE__;
    }
    /* RX restore failures must take precedence over the missing ACK. */
    for (unsigned failure=0; failure<2; ++failure) {
        txAttempted=false;
        txOutcome=0x10u;
        restoreFailure=failure ? HAL_TIMEOUT : HAL_ERROR;
        failRestore=true;
        NRF24_Status_t expected=failure ? NRF24_TIMEOUT : NRF24_ERROR;
        if (NrfLink_SendFrame(&link,destination,frame,sizeof(frame),100u)!=expected ||
            listening || failRestore) return __LINE__;
    }
    uint8_t received[32], length=99u;
    failFifo=true;
    if (NrfLink_ReceiveFrame(&link,received,sizeof(received),&length)!=NRF24_TIMEOUT ||
        length!=0u || selected) return __LINE__;
    failFifo=false; regs[23][0]=0x11u;
    if (NrfLink_ReceiveFrame(&link,received,sizeof(received),&length)!=NRF24_OK ||
        length!=0u) return __LINE__;
    return 0;
}
