#include "SecureSessionStorage.h"

#include "stm32f4xx_hal.h"

#define SECURE_SESSION_FLASH_ADDRESS     0x08060000u
#define SECURE_SESSION_FLASH_END_ADDRESS 0x08080000u
#define SECURE_SESSION_MAX_COUNTER       0x00FFFFFFu
#define SECURE_SESSION_EMPTY_WORD        0xFFFFFFFFu

static bool ProgramCounter(uint32_t address, uint32_t counter);
static bool EraseStorageSector(void);

bool SecureSessionStorage_NextBootCounter(uint32_t *bootCounter)
{
    uint32_t address;
    uint32_t lastCounter = 0u;
    uint32_t nextCounter;
    uint32_t freeAddress = SECURE_SESSION_FLASH_END_ADDRESS;

    if (bootCounter == NULL)
    {
        return false;
    }

    for (address = SECURE_SESSION_FLASH_ADDRESS;
         address < SECURE_SESSION_FLASH_END_ADDRESS;
         address += sizeof(uint32_t))
    {
        uint32_t value = *(const volatile uint32_t *)address;

        if (value == SECURE_SESSION_EMPTY_WORD)
        {
            freeAddress = address;
            break;
        }

        if (value == 0u || value > SECURE_SESSION_MAX_COUNTER)
        {
            return false;
        }

        lastCounter = value;
    }

    if (lastCounter >= SECURE_SESSION_MAX_COUNTER)
    {
        return false;
    }

    nextCounter = lastCounter + 1u;

    if (freeAddress >= SECURE_SESSION_FLASH_END_ADDRESS)
    {
        if (!EraseStorageSector())
        {
            return false;
        }
        freeAddress = SECURE_SESSION_FLASH_ADDRESS;
    }

    if (!ProgramCounter(freeAddress, nextCounter))
    {
        return false;
    }

    *bootCounter = nextCounter;
    return true;
}

static bool ProgramCounter(uint32_t address, uint32_t counter)
{
    HAL_StatusTypeDef status;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, counter);
    (void)HAL_FLASH_Lock();

    if (status != HAL_OK)
    {
        return false;
    }

    return *(const volatile uint32_t *)address == counter;
}

static bool EraseStorageSector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sectorError = 0u;
    HAL_StatusTypeDef status;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_7;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASHEx_Erase(&erase, &sectorError);
    (void)HAL_FLASH_Lock();

    return status == HAL_OK && sectorError == 0xFFFFFFFFu;
}
