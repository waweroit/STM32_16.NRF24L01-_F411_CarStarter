#ifndef TEST_PORT_H
#define TEST_PORT_H
#include <stdbool.h>
#include <stdint.h>
/* NOR is emulated; production CarStorage.c and CarEngine.c remain unchanged. */
#define W25Q64_H
#define W25Q64_SECTOR_SIZE 4096u
typedef struct { unsigned unused; } W25Q_HandleTypeDef;
bool W25Q_Read(W25Q_HandleTypeDef *, uint32_t, uint8_t *, uint32_t);
bool W25Q_Write(W25Q_HandleTypeDef *, uint32_t, const uint8_t *, uint32_t);
bool W25Q_EraseSector(W25Q_HandleTypeDef *, uint32_t);
#endif
