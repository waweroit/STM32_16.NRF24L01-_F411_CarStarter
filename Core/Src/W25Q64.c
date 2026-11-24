/* SPI NOR port based on eeW25Q64_onF411CEU6. Erase/program are deliberately
 * separate; all transactions have bounded waits and propagate HAL failures.
 * Only the main loop owns SPI1. ISRs must never use this port or the radio. */
#include "W25Q64.h"
#include <string.h>
#define SPI_TIMEOUT 20u

/** Steruje linią CS pamięci d. selected równe true wybiera układ stanem niskim. */
static void Select(W25Q_HandleTypeDef *d, bool selected)
{
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
/** Wysyła jednobajtowe polecenie cmd do pamięci d i zwraca wynik transmisji. */
static bool Command(W25Q_HandleTypeDef *d, uint8_t cmd)
{
    Select(d, true);
    bool ok = HAL_SPI_Transmit(d->hspi, &cmd, 1u, SPI_TIMEOUT) == HAL_OK;
    Select(d, false);
    return ok;
}
/** Odczytuje pierwszy rejestr stanu pamięci d. Przy powodzeniu zapisuje bajt pod adresem
 * value. */
static bool Status(W25Q_HandleTypeDef *d, uint8_t *value)
{
    uint8_t cmd = 0x05u;
    Select(d, true);
    bool ok = HAL_SPI_Transmit(d->hspi, &cmd, 1u, SPI_TIMEOUT) == HAL_OK &&
              HAL_SPI_Receive(d->hspi, value, 1u, SPI_TIMEOUT) == HAL_OK;
    Select(d, false);
    return ok;
}
/** Czeka na wyzerowanie bitu BUSY pamięci d. Zwraca false przy błędzie SPI lub po
 * przekroczeniu timeout (ms). */
static const char *WaitReadyError(W25Q_HandleTypeDef *d, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    do {
        if (!Status(d, &d->lastStatus)) return "status SPI";
        if (!(d->lastStatus & 1u)) return NULL;
    } while ((uint32_t)(HAL_GetTick() - start) < timeout);
    return "BUSY timeout";
}
static bool Ready(W25Q_HandleTypeDef *d, uint32_t timeout)
{
    return WaitReadyError(d, timeout) == NULL;
}
/** Odblokowuje pojedynczą operację zapisu lub kasowania pamięci d i sprawdza bit WEL. */
static bool WriteEnable(W25Q_HandleTypeDef *d)
{
    uint8_t status;
    return Ready(d, 500u) && Command(d, 0x06u) && Status(d, &status) && (status & 2u);
}
/** Wysyła do wybranej pamięci d kod polecenia cmd i trzy bajty adresu addr, od najstarszego.
 * Zwraca wynik transmisji nagłówka. */
static bool Address(W25Q_HandleTypeDef *d, uint8_t cmd, uint32_t addr)
{
    uint8_t header[] = {cmd, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    return HAL_SPI_Transmit(d->hspi, header, sizeof(header), SPI_TIMEOUT) == HAL_OK;
}
/** Wybudza pamięć d z trybu obniżonego poboru energii i sprawdza jej gotowość. */
bool W25Q_Wake(W25Q_HandleTypeDef *d)
{
    d->initError = "wake SPI";
    if (!Command(d, 0xABu)) return false;
    HAL_Delay(1u);
    d->initError = WaitReadyError(d, 500u);
    return d->initError == NULL;
}
/** Wypełnia uchwyt d parametrami magistrali spi i linii CS (port, maska pin). Zwraca true po
 * wybudzeniu i sprawdzeniu producenta oraz pojemności. */
bool W25Q_Init(W25Q_HandleTypeDef *d, SPI_HandleTypeDef *spi, GPIO_TypeDef *port, uint16_t pin)
{
    if (!d || !spi || !port) return false;
    d->hspi = spi; d->cs_port = port; d->cs_pin = pin;
    Select(d, false);
    if (!W25Q_Wake(d)) return false;
    uint8_t cmd = 0x9Fu;
    Select(d, true);
    bool ok = HAL_SPI_Transmit(spi, &cmd, 1u, SPI_TIMEOUT) == HAL_OK &&
              HAL_SPI_Receive(spi, d->jedecId, sizeof(d->jedecId), SPI_TIMEOUT) == HAL_OK;
    Select(d, false);
    /* Winbond W25Q64: manufacturer EF, density 17; memory type depends on variant. */
    if (!ok) d->initError = "JEDEC SPI";
    else if (d->jedecId[0] != 0xEFu || d->jedecId[2] != 0x17u) d->initError = "JEDEC mismatch";
    return d->initError == NULL;
}
/** Czyta len bajtów od adresu addr pamięci d do bufora data. Zwraca true po odczycie całego
 * zakresu. */
bool W25Q_Read(W25Q_HandleTypeDef *d, uint32_t addr, uint8_t *data, uint32_t len)
{
    if (!data || addr >= W25Q64_SIZE || len > W25Q64_SIZE - addr || !Ready(d, 500u)) return false;
    while (len) {
        uint16_t n = len > 256u ? 256u : (uint16_t)len;
        Select(d, true);
        bool ok = Address(d, 0x03u, addr) && HAL_SPI_Receive(d->hspi, data, n, SPI_TIMEOUT) == HAL_OK;
        Select(d, false);
        if (!ok) return false;
        addr += n; data += n; len -= n;
    }
    return true;
}
/** Programuje len bajtów z data od adresu addr w pamięci d. Obszar powinien być wcześniej
 * skasowany. Zwraca true po weryfikacji wszystkich zapisanych fragmentów. */
bool W25Q_Write(W25Q_HandleTypeDef *d, uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!data || addr >= W25Q64_SIZE || len > W25Q64_SIZE - addr) return false;
    uint8_t verify[W25Q64_PAGE_SIZE];
    while (len) {
        uint32_t n = W25Q64_PAGE_SIZE - addr % W25Q64_PAGE_SIZE;
        if (n > len) n = len;
        if (!WriteEnable(d)) return false;
        Select(d, true);
        bool ok = Address(d, 0x02u, addr) &&
                  HAL_SPI_Transmit(d->hspi, (uint8_t *)data, (uint16_t)n, SPI_TIMEOUT) == HAL_OK;
        Select(d, false);
        if (!ok || !Ready(d, 10u) || !W25Q_Read(d, addr, verify, n) || memcmp(data, verify, n)) return false;
        addr += n; data += n; len -= n;
    }
    return true;
}
/** Kasuje sektor pamięci d rozpoczynający się pod wyrównanym adresem addr. Zwraca true po
 * zakończeniu operacji. */
bool W25Q_EraseSector(W25Q_HandleTypeDef *d, uint32_t addr)
{
    if (addr >= W25Q64_SIZE || addr % W25Q64_SECTOR_SIZE || !WriteEnable(d)) return false;
    Select(d, true);
    bool ok = Address(d, 0x20u, addr);
    Select(d, false);
    return ok && Ready(d, 500u);
}
/** Czeka na zakończenie operacji pamięci d i wysyła polecenie uśpienia. Zwraca wynik tej
 * próby. */
bool W25Q_PowerDown(W25Q_HandleTypeDef *d)
{
    return Ready(d, 500u) && Command(d, 0xB9u);
}
