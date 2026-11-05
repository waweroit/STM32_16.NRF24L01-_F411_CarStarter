#ifndef W25Q64_H
#define W25Q64_H
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#define W25Q64_SIZE (8u * 1024u * 1024u)
#define W25Q64_PAGE_SIZE 256u
#define W25Q64_SECTOR_SIZE 4096u
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
} W25Q_HandleTypeDef;
bool W25Q_Init(W25Q_HandleTypeDef *dev, SPI_HandleTypeDef *spi, GPIO_TypeDef *port, uint16_t pin);
bool W25Q_Read(W25Q_HandleTypeDef *dev, uint32_t addr, uint8_t *data, uint32_t len);
/* Programs erased bytes only. NEVER erases a sector implicitly. */
bool W25Q_Write(W25Q_HandleTypeDef *dev, uint32_t addr, const uint8_t *data, uint32_t len);
bool W25Q_EraseSector(W25Q_HandleTypeDef *dev, uint32_t addr);
bool W25Q_PowerDown(W25Q_HandleTypeDef *dev);
bool W25Q_Wake(W25Q_HandleTypeDef *dev);
#endif
