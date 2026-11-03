#ifndef NRF24L01_H
#define NRF24L01_H

#include <stdbool.h>
#include <stdint.h>

#ifndef NRF24_HAL_HEADER
#define NRF24_HAL_HEADER "stm32f4xx_hal.h"
#endif
#include NRF24_HAL_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#define NRF24_MAX_PAYLOAD_SIZE 32u

/** @brief nRF24L01+ driver result. */
typedef enum {
    NRF24_OK = 0,
    NRF24_ERROR,
    NRF24_TIMEOUT,
    NRF24_MAX_RETRY,
    NRF24_INVALID_ARGUMENT,
    NRF24_INVALID_PAYLOAD_WIDTH
} NRF24_Status_t;

/** @brief Supported air data rates. */
typedef enum {
    NRF24_DATA_RATE_1MBPS = 0,
    NRF24_DATA_RATE_2MBPS,
    NRF24_DATA_RATE_250KBPS
} NRF24_DataRate_t;

/** @brief nRF24L01+ PA output levels. */
typedef enum {
    NRF24_POWER_MINUS_18_DBM = 0,
    NRF24_POWER_MINUS_12_DBM,
    NRF24_POWER_MINUS_6_DBM,
    NRF24_POWER_0_DBM
} NRF24_Power_t;

/** @brief Driver instance. Protect one instance and its SPI bus with a mutex when shared. */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cePort;
    uint16_t cePin;
    GPIO_TypeDef *csnPort;
    uint16_t csnPin;
    uint8_t channel;
    uint8_t payloadSize;
    uint8_t addressWidth;
    bool dynamicPayloadEnabled;
} NRF24_Handle_t;

/** @brief Initialize radio defaults: channel 76, 1 Mbps, 0 dBm, 5-byte addresses, CRC-16, Auto ACK and dynamic payload. */
NRF24_Status_t NRF24_Init(NRF24_Handle_t *handle, SPI_HandleTypeDef *hspi,
                          GPIO_TypeDef *cePort, uint16_t cePin,
                          GPIO_TypeDef *csnPort, uint16_t csnPin);
/** @brief Read one nRF register. */
NRF24_Status_t NRF24_ReadRegister(NRF24_Handle_t *handle, uint8_t reg, uint8_t *value);
/** @brief Write one nRF register. */
NRF24_Status_t NRF24_WriteRegister(NRF24_Handle_t *handle, uint8_t reg, uint8_t value);
/** @brief Read a multibyte register. */
NRF24_Status_t NRF24_ReadBuffer(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buffer, uint8_t length);
/** @brief Write a multibyte register. */
NRF24_Status_t NRF24_WriteBuffer(NRF24_Handle_t *handle, uint8_t reg, const uint8_t *buffer, uint8_t length);
/** @brief Set RF channel in range 0..125. */
NRF24_Status_t NRF24_SetChannel(NRF24_Handle_t *handle, uint8_t channel);
/** @brief Set transmitter output power. */
NRF24_Status_t NRF24_SetPower(NRF24_Handle_t *handle, NRF24_Power_t power);
/** @brief Set 250 kbps, 1 Mbps or 2 Mbps data rate. */
NRF24_Status_t NRF24_SetDataRate(NRF24_Handle_t *handle, NRF24_DataRate_t rate);
/** @brief Set address width to 3, 4 or 5 bytes. */
NRF24_Status_t NRF24_SetAddressWidth(NRF24_Handle_t *handle, uint8_t width);
/** @brief Set TX address using the configured address width. */
NRF24_Status_t NRF24_SetTxAddress(NRF24_Handle_t *handle, const uint8_t *address);
/** @brief Set RX address for pipe 0..5. Pipes 2..5 use address[0] only. */
NRF24_Status_t NRF24_SetRxAddress(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *address);
/** @brief Enter powered RX mode and assert CE. */
NRF24_Status_t NRF24_StartListening(NRF24_Handle_t *handle);
/** @brief Deassert CE and enter powered TX mode. */
NRF24_Status_t NRF24_StopListening(NRF24_Handle_t *handle);
/** @brief Send one 1..32-byte payload and wait for TX_DS/MAX_RT/timeout. */
NRF24_Status_t NRF24_Send(NRF24_Handle_t *handle, const uint8_t *data, uint8_t length, uint32_t timeoutMs);
/** @brief Receive one payload. receivedLength is zero when no payload is queued. */
NRF24_Status_t NRF24_Receive(NRF24_Handle_t *handle, uint8_t *data, uint8_t bufferSize, uint8_t *receivedLength);
/** @brief Return true when RX FIFO contains data; optionally return pipe number. */
bool NRF24_DataAvailable(NRF24_Handle_t *handle, uint8_t *pipeNumber);
/** @brief Clear RX_DR, TX_DS and MAX_RT flags. */
NRF24_Status_t NRF24_ClearIrqFlags(NRF24_Handle_t *handle);
/** @brief Flush TX FIFO. */
NRF24_Status_t NRF24_FlushTx(NRF24_Handle_t *handle);
/** @brief Flush RX FIFO. */
NRF24_Status_t NRF24_FlushRx(NRF24_Handle_t *handle);
/** @brief Convert status to a static diagnostic string. */
const char *NRF24_StatusToString(NRF24_Status_t status);

#ifdef __cplusplus
}
#endif
#endif
