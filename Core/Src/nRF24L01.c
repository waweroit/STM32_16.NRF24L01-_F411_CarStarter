#include "nRF24L01.h"
#include <stddef.h>

#define CMD_R_REGISTER       0x00u
#define CMD_W_REGISTER       0x20u
#define CMD_R_RX_PAYLOAD     0x61u
#define CMD_W_TX_PAYLOAD     0xA0u
#define CMD_FLUSH_TX         0xE1u
#define CMD_FLUSH_RX         0xE2u
#define CMD_R_RX_PL_WID      0x60u
#define CMD_ACTIVATE         0x50u
#define CMD_NOP              0xFFu

#define REG_CONFIG           0x00u
#define REG_EN_AA            0x01u
#define REG_EN_RXADDR        0x02u
#define REG_SETUP_AW         0x03u
#define REG_SETUP_RETR       0x04u
#define NRF24_DEFAULT_CHANNEL 100u
#define REG_RF_CH            0x05u
#define REG_RF_SETUP         0x06u
#define REG_STATUS           0x07u
#define REG_RX_ADDR_P0       0x0Au
#define REG_RX_PW_P0         0x11u
#define REG_FIFO_STATUS      0x17u
#define REG_DYNPD            0x1Cu
#define REG_FEATURE          0x1Du
#define REG_TX_ADDR          0x10u

#define BIT_MASK_RX_DR       0x40u
#define BIT_MASK_TX_DS       0x20u
#define BIT_MASK_MAX_RT      0x10u
#define BIT_EN_CRC           0x08u
#define BIT_CRCO             0x04u
#define BIT_PWR_UP           0x02u
#define BIT_PRIM_RX          0x01u
#define BIT_RF_DR_LOW        0x20u
#define BIT_RF_DR_HIGH       0x08u
#define BIT_EN_DPL           0x04u
#define FIFO_RX_EMPTY        0x01u

/** Ustawia stan pinu CE, który uruchamia nasłuch w trybie RX lub wyzwala nadawanie w trybie TX. */
static void set_ce(NRF24_Handle_t *handle, GPIO_PinState state)
{
    HAL_GPIO_WritePin(handle->cePort, handle->cePin, state);
}

/** Ustawia stan pinu CSN. Stan niski rozpoczyna transakcję SPI, a wysoki ją kończy. */
static void set_csn(NRF24_Handle_t *handle, GPIO_PinState state)
{
    HAL_GPIO_WritePin(handle->csnPort, handle->csnPin, state);
}

/** Wysyła i odbiera dane przez SPI z limitem 100 ms i zwraca wynik w postaci statusu sterownika. */
static NRF24_Status_t spi_exchange(NRF24_Handle_t *handle,
                                   const uint8_t *tx,
                                   uint8_t *rx,
                                   uint16_t length)
{
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(handle->hspi,
                                                       (uint8_t *)tx,
                                                       rx,
                                                       length,
                                                       100u);
    if (status == HAL_OK) {
        return NRF24_OK;
    }
    return status == HAL_TIMEOUT ? NRF24_TIMEOUT : NRF24_ERROR;
}

/** Wykonuje komendę i przesyła jej dane w jednej transakcji SPI. Opcjonalnie zapisuje bajt STATUS i odebrane dane. */
static NRF24_Status_t execute_command(NRF24_Handle_t *handle,
                                      uint8_t command,
                                      const uint8_t *tx,
                                      uint8_t *rx,
                                      uint8_t length,
                                      uint8_t *statusByte)
{
    uint8_t response;
    uint8_t i;
    NRF24_Status_t status;

    set_csn(handle, GPIO_PIN_RESET);
    status = spi_exchange(handle, &command, &response, 1u);
    if (statusByte != NULL && status == NRF24_OK) {
        *statusByte = response;
    }

    for (i = 0u; i < length && status == NRF24_OK; ++i) {
        uint8_t outgoing = tx != NULL ? tx[i] : CMD_NOP;
        status = spi_exchange(handle, &outgoing, &response, 1u);
        if (rx != NULL && status == NRF24_OK) {
            rx[i] = response;
        }
    }
    set_csn(handle, GPIO_PIN_SET);
    return status;
}

/** Odczytuje jeden bajt z rejestru reg i zapisuje go pod adresem value. */
NRF24_Status_t NRF24_ReadRegister(NRF24_Handle_t *handle, uint8_t reg, uint8_t *value)
{
    if (handle == NULL || value == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, (uint8_t)(CMD_R_REGISTER | (reg & 0x1Fu)),
                           NULL, value, 1u, NULL);
}

/** Zapisuje bajt value do rejestru reg. */
NRF24_Status_t NRF24_WriteRegister(NRF24_Handle_t *handle, uint8_t reg, uint8_t value)
{
    if (handle == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, (uint8_t)(CMD_W_REGISTER | (reg & 0x1Fu)),
                           &value, NULL, 1u, NULL);
}

/** Odczytuje z rejestru wielobajtowego reg do bufora buffer liczbę bajtów podaną w length. */
NRF24_Status_t NRF24_ReadBuffer(NRF24_Handle_t *handle, uint8_t reg,
                                uint8_t *buffer, uint8_t length)
{
    if (handle == NULL || buffer == NULL || length == 0u) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, (uint8_t)(CMD_R_REGISTER | (reg & 0x1Fu)),
                           NULL, buffer, length, NULL);
}

/** Zapisuje do rejestru wielobajtowego reg liczbę bajtów podaną w length, pobierając dane z buffer. */
NRF24_Status_t NRF24_WriteBuffer(NRF24_Handle_t *handle, uint8_t reg,
                                 const uint8_t *buffer, uint8_t length)
{
    if (handle == NULL || buffer == NULL || length == 0u) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, (uint8_t)(CMD_W_REGISTER | (reg & 0x1Fu)),
                           buffer, NULL, length, NULL);
}

/** Usuwa wszystkie pakiety oczekujące w kolejce nadawczej TX FIFO. */
NRF24_Status_t NRF24_FlushTx(NRF24_Handle_t *handle)
{
    if (handle == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, CMD_FLUSH_TX, NULL, NULL, 0u, NULL);
}

/** Usuwa wszystkie nieodczytane pakiety z kolejki odbiorczej RX FIFO. */
NRF24_Status_t NRF24_FlushRx(NRF24_Handle_t *handle)
{
    if (handle == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    return execute_command(handle, CMD_FLUSH_RX, NULL, NULL, 0u, NULL);
}

/** Kasuje flagi przerwań RX_DR (odbiór), TX_DS (wysłanie) i MAX_RT (wyczerpanie retransmisji). */
NRF24_Status_t NRF24_ClearIrqFlags(NRF24_Handle_t *handle)
{
    return NRF24_WriteRegister(handle, REG_STATUS,
                               BIT_MASK_RX_DR | BIT_MASK_TX_DS | BIT_MASK_MAX_RT);
}

/** Ustawia kanał radiowy z zakresu 0–125 i po udanym zapisie aktualizuje handle->channel. */
NRF24_Status_t NRF24_SetChannel(NRF24_Handle_t *handle, uint8_t channel)
{
    NRF24_Status_t status;
    if (handle == NULL || channel > 125u) {
        return NRF24_INVALID_ARGUMENT;
    }
    status = NRF24_WriteRegister(handle, REG_RF_CH, channel);
    if (status == NRF24_OK) {
        handle->channel = channel;
    }
    return status;
}

/** Ustawia długość adresu radiowego na 3, 4 lub 5 bajtów i zapamiętuje ją w uchwycie sterownika. */
NRF24_Status_t NRF24_SetAddressWidth(NRF24_Handle_t *handle, uint8_t width)
{
    NRF24_Status_t status;
    if (handle == NULL || width < 3u || width > 5u) {
        return NRF24_INVALID_ARGUMENT;
    }
    status = NRF24_WriteRegister(handle, REG_SETUP_AW, (uint8_t)(width - 2u));
    if (status == NRF24_OK) {
        handle->addressWidth = width;
    }
    return status;
}

/** Ustawia moc nadajnika za pomocą bitów RF_PWR w rejestrze RF_SETUP. */
NRF24_Status_t NRF24_SetPower(NRF24_Handle_t *handle, NRF24_Power_t power)
{
    uint8_t value;
    NRF24_Status_t status;
    if (handle == NULL || power > NRF24_POWER_0_DBM) {
        return NRF24_INVALID_ARGUMENT;
    }
    status = NRF24_ReadRegister(handle, REG_RF_SETUP, &value);
    if (status != NRF24_OK) {
        return status;
    }
    value = (uint8_t)((value & ~0x06u) | ((uint8_t)power << 1u));
    return NRF24_WriteRegister(handle, REG_RF_SETUP, value);
}

/** Ustawia prędkość transmisji radiowej w rejestrze RF_SETUP.
 * Dla 250 kbit/s ustawia RF_DR_LOW (bit 5), dla 2 Mbit/s ustawia RF_DR_HIGH (bit 3),
 * a dla 1 Mbit/s zeruje oba bity. Oba końce łącza muszą używać tej samej prędkości. */
NRF24_Status_t NRF24_SetDataRate(NRF24_Handle_t *handle, NRF24_DataRate_t rate)
{
    uint8_t value;
    NRF24_Status_t status;
    if (handle == NULL || rate > NRF24_DATA_RATE_250KBPS) {
        return NRF24_INVALID_ARGUMENT;
    }
    status = NRF24_ReadRegister(handle, REG_RF_SETUP, &value);
    if (status != NRF24_OK) {
        return status;
    }
    value &= (uint8_t)~(BIT_RF_DR_LOW | BIT_RF_DR_HIGH);
    if (rate == NRF24_DATA_RATE_2MBPS) {
        value |= BIT_RF_DR_HIGH;
    } else if (rate == NRF24_DATA_RATE_250KBPS) {
        value |= BIT_RF_DR_LOW;
    }
    return NRF24_WriteRegister(handle, REG_RF_SETUP, value);
}

/** Zapisuje adres docelowy nadajnika do TX_ADDR, używając skonfigurowanej długości adresu. */
NRF24_Status_t NRF24_SetTxAddress(NRF24_Handle_t *handle, const uint8_t *address)
{
    if (handle == NULL || address == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    return NRF24_WriteBuffer(handle, REG_TX_ADDR, address, handle->addressWidth);
}

/** Ustawia adres odbiorczy dla ścieżki pipe. Dla ścieżek 0 i 1 zapisuje cały adres,
 * a dla ścieżek 2–5 tylko address[0], ponieważ pozostałe bajty są wspólne ze ścieżką 1. */
NRF24_Status_t NRF24_SetRxAddress(NRF24_Handle_t *handle, uint8_t pipe,
                                  const uint8_t *address)
{
    if (handle == NULL || address == NULL || pipe > 5u) {
        return NRF24_INVALID_ARGUMENT;
    }
    if (pipe <= 1u) {
        return NRF24_WriteBuffer(handle, (uint8_t)(REG_RX_ADDR_P0 + pipe),
                                 address, handle->addressWidth);
    }
    return NRF24_WriteRegister(handle, (uint8_t)(REG_RX_ADDR_P0 + pipe), address[0]);
}

/** Włącza obsługę pakietów o zmiennej długości i sprawdza bit EN_DPL. W razie potrzeby odblokowuje rejestr FEATURE komendą ACTIVATE. */
static NRF24_Status_t enable_dynamic_payload(NRF24_Handle_t *handle)
{
    uint8_t value;
    uint8_t activateData = 0x73u;
    NRF24_Status_t status;

    status = NRF24_WriteRegister(handle, REG_FEATURE, BIT_EN_DPL);
    if (status != NRF24_OK) {
        return status;
    }
    status = NRF24_ReadRegister(handle, REG_FEATURE, &value);
    if (status != NRF24_OK) {
        return status;
    }

    if ((value & BIT_EN_DPL) == 0u) {
        status = execute_command(handle, CMD_ACTIVATE, &activateData, NULL, 1u, NULL);
        if (status != NRF24_OK) {
            return status;
        }
        status = NRF24_WriteRegister(handle, REG_FEATURE, BIT_EN_DPL);
        if (status != NRF24_OK) {
            return status;
        }
        status = NRF24_ReadRegister(handle, REG_FEATURE, &value);
        if (status != NRF24_OK) {
            return status;
        }
    }
    return (value & BIT_EN_DPL) != 0u ? NRF24_OK : NRF24_ERROR;
}

/** Przypisuje interfejs SPI i piny CE/CSN, ustawia kanał, prędkość i moc radia.
 * Włącza automatyczne potwierdzenia i pakiety o zmiennej długości na ścieżce 0,
 * opróżnia kolejki i pozostawia radio w gotowości do nadawania. */
NRF24_Status_t NRF24_Init(NRF24_Handle_t *handle, SPI_HandleTypeDef *hspi,
                          GPIO_TypeDef *cePort, uint16_t cePin,
                          GPIO_TypeDef *csnPort, uint16_t csnPin)
{
    NRF24_Status_t status;
    uint8_t verify;

    if (handle == NULL || hspi == NULL || cePort == NULL || csnPort == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }

    handle->hspi = hspi;
    handle->cePort = cePort;
    handle->cePin = cePin;
    handle->csnPort = csnPort;
    handle->csnPin = csnPin;
    handle->channel = NRF24_DEFAULT_CHANNEL;
    handle->payloadSize = 32u;
    handle->addressWidth = 5u;
    handle->dynamicPayloadEnabled = false;

    set_ce(handle, GPIO_PIN_RESET);
    set_csn(handle, GPIO_PIN_SET);
    HAL_Delay(5u);

    status = NRF24_WriteRegister(handle, REG_CONFIG, 0u);
    if (status != NRF24_OK) return status;
    status = NRF24_WriteRegister(handle, REG_EN_AA, 0x01u);
    if (status != NRF24_OK) return status;
    status = NRF24_WriteRegister(handle, REG_EN_RXADDR, 0x01u);
    if (status != NRF24_OK) return status;
    status = NRF24_WriteRegister(handle, REG_SETUP_RETR, 0x2Fu);
    if (status != NRF24_OK) return status;
    status = NRF24_SetAddressWidth(handle, 5u);
    if (status != NRF24_OK) return status;
    status = NRF24_SetChannel(handle, NRF24_DEFAULT_CHANNEL);
    if (status != NRF24_OK) return status;
    status = NRF24_SetDataRate(handle, NRF24_DATA_RATE_250KBPS);
    if (status != NRF24_OK) return status;
    status = NRF24_SetPower(handle, NRF24_POWER_0_DBM);
    if (status != NRF24_OK) return status;
    status = enable_dynamic_payload(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_WriteRegister(handle, REG_DYNPD, 0x01u);
    if (status != NRF24_OK) return status;
    /* EN_DPL alone is insufficient: pipe 0 must latch DPL_P0 too. */
    status = NRF24_ReadRegister(handle, REG_DYNPD, &verify);
    if (status != NRF24_OK) return status;
    if (verify != 0x01u) return NRF24_ERROR;
    status = NRF24_ReadRegister(handle, REG_FEATURE, &verify);
    if (status != NRF24_OK) return status;
    if (verify != BIT_EN_DPL) return NRF24_ERROR;
    handle->dynamicPayloadEnabled = true;
    status = NRF24_WriteRegister(handle, REG_RX_PW_P0, 32u);
    if (status != NRF24_OK) return status;
    status = NRF24_ClearIrqFlags(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_FlushRx(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_FlushTx(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_WriteRegister(handle, REG_CONFIG,
                                 BIT_EN_CRC | BIT_CRCO | BIT_PWR_UP);
    if (status != NRF24_OK) return status;

    status = NRF24_ReadRegister(handle, REG_RF_CH, &verify);
    if (status != NRF24_OK || verify != NRF24_DEFAULT_CHANNEL) {
        return NRF24_ERROR;
    }
    status = NRF24_ReadRegister(handle, REG_CONFIG, &verify);
    if (status != NRF24_OK || verify != (BIT_EN_CRC | BIT_CRCO | BIT_PWR_UP)) {
        return NRF24_ERROR;
    }
    HAL_Delay(5u);
    return NRF24_OK;
}

/** Usypia radio przez wyzerowanie bitu PWR_UP i potwierdza zmianę odczytem rejestru CONFIG. */
NRF24_Status_t NRF24_PowerDown(NRF24_Handle_t *handle)
{
    uint8_t config, verify;
    if (handle == NULL) return NRF24_INVALID_ARGUMENT;
    set_ce(handle, GPIO_PIN_RESET);
    NRF24_Status_t status = NRF24_ReadRegister(handle, REG_CONFIG, &config);
    if (status != NRF24_OK) return status;
    config = (uint8_t)(config & ~BIT_PWR_UP);
    status = NRF24_WriteRegister(handle, REG_CONFIG, config);
    if (status != NRF24_OK) return status;
    status = NRF24_ReadRegister(handle, REG_CONFIG, &verify);
    if (status != NRF24_OK) return status;
    return verify == config ? NRF24_OK : NRF24_ERROR;
}

/** Przełącza radio w tryb odbioru, kasuje flagi przerwań i rozpoczyna nasłuch, ustawiając CE w stan wysoki. */
NRF24_Status_t NRF24_StartListening(NRF24_Handle_t *handle)
{
    uint8_t config;
    NRF24_Status_t status;
    if (handle == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    set_ce(handle, GPIO_PIN_RESET);
    status = NRF24_ReadRegister(handle, REG_CONFIG, &config);
    if (status != NRF24_OK) return status;
    bool poweringUp = (config & BIT_PWR_UP) == 0u;
    config |= BIT_PWR_UP | BIT_PRIM_RX;
    status = NRF24_WriteRegister(handle, REG_CONFIG, config);
    if (status != NRF24_OK) return status;
    status = NRF24_ClearIrqFlags(handle);
    if (status != NRF24_OK) return status;
    if (poweringUp) HAL_Delay(2u); /* 1.5 ms oscillator startup after power-down. */
    set_ce(handle, GPIO_PIN_SET);
    HAL_Delay(1u);
    return NRF24_OK;
}

/** Kończy nasłuch, ustawiając CE w stan niski, i przełącza radio w gotowość do nadawania. */
NRF24_Status_t NRF24_StopListening(NRF24_Handle_t *handle)
{
    uint8_t config;
    NRF24_Status_t status;
    if (handle == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    set_ce(handle, GPIO_PIN_RESET);
    status = NRF24_ReadRegister(handle, REG_CONFIG, &config);
    if (status != NRF24_OK) return status;
    bool poweringUp = (config & BIT_PWR_UP) == 0u;
    config = (uint8_t)((config | BIT_PWR_UP) & ~BIT_PRIM_RX);
    status = NRF24_WriteRegister(handle, REG_CONFIG, config);
    if (status == NRF24_OK) {
        HAL_Delay(poweringUp ? 2u : 1u);
    }
    return status;
}

/** Sprawdza, czy kolejka RX zawiera pakiet. Opcjonalnie zapisuje numer jego ścieżki w pipeNumber.
 * Zwraca false także przy błędzie odczytu rejestrów. */
bool NRF24_DataAvailable(NRF24_Handle_t *handle, uint8_t *pipeNumber)
{
    uint8_t fifoStatus;
    uint8_t status;
    if (handle == NULL) {
        return false;
    }
    if (NRF24_ReadRegister(handle, REG_FIFO_STATUS, &fifoStatus) != NRF24_OK ||
        (fifoStatus & FIFO_RX_EMPTY) != 0u) {
        return false;
    }
    if (NRF24_ReadRegister(handle, REG_STATUS, &status) != NRF24_OK) {
        return false;
    }
    if (pipeNumber != NULL) {
        *pipeNumber = (uint8_t)((status >> 1u) & 0x07u);
    }
    return true;
}

/** Kończy nasłuch, opróżnia kolejkę TX i nadaje pakiet o długości 1–32 bajtów.
 * Czeka na wynik transmisji. Zwraca NRF24_MAX_RETRY po wyczerpaniu retransmisji
 * lub NRF24_TIMEOUT po upływie czasu oczekiwania timeoutMs. */
NRF24_Status_t NRF24_Send(NRF24_Handle_t *handle, const uint8_t *data,
                          uint8_t length, uint32_t timeoutMs)
{
    uint32_t start;
    uint8_t statusRegister;
    NRF24_Status_t status;

    if (handle == NULL || data == NULL || length == 0u ||
        length > NRF24_MAX_PAYLOAD_SIZE || timeoutMs == 0u) {
        return NRF24_INVALID_ARGUMENT;
    }

    status = NRF24_StopListening(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_ClearIrqFlags(handle);
    if (status != NRF24_OK) return status;
    status = NRF24_FlushTx(handle);
    if (status != NRF24_OK) return status;
    status = execute_command(handle, CMD_W_TX_PAYLOAD, data, NULL, length, NULL);
    if (status != NRF24_OK) return status;

    set_ce(handle, GPIO_PIN_SET);
    HAL_Delay(1u); /* Safely exceeds the 10 us minimum CE pulse. */
    set_ce(handle, GPIO_PIN_RESET);

    start = HAL_GetTick();
    for (;;) {
        status = NRF24_ReadRegister(handle, REG_STATUS, &statusRegister);
        if (status != NRF24_OK) return status;
        if ((statusRegister & BIT_MASK_TX_DS) != 0u) {
            (void)NRF24_WriteRegister(handle, REG_STATUS, BIT_MASK_TX_DS);
            return NRF24_OK;
        }
        if ((statusRegister & BIT_MASK_MAX_RT) != 0u) {
            (void)NRF24_WriteRegister(handle, REG_STATUS, BIT_MASK_MAX_RT);
            (void)NRF24_FlushTx(handle);
            return NRF24_MAX_RETRY;
        }
        if ((HAL_GetTick() - start) >= timeoutMs) {
            (void)NRF24_ClearIrqFlags(handle);
            (void)NRF24_FlushTx(handle);
            return NRF24_TIMEOUT;
        }
        HAL_Delay(1u);
    }
}

/** Pobiera jeden pakiet do data i zapisuje jego długość w receivedLength.
 * Przy pustej kolejce zwraca NRF24_OK i długość 0. Błędna długość pakietu
 * lub zbyt mały bufor powodują opróżnienie całej kolejki RX. */
NRF24_Status_t NRF24_Receive(NRF24_Handle_t *handle, uint8_t *data,
                             uint8_t bufferSize, uint8_t *receivedLength)
{
    uint8_t width;
    NRF24_Status_t status;
    if (handle == NULL || data == NULL || receivedLength == NULL) {
        return NRF24_INVALID_ARGUMENT;
    }
    *receivedLength = 0u;
    status = NRF24_ReadRegister(handle, REG_FIFO_STATUS, &width);
    if (status != NRF24_OK) return status;
    if ((width & FIFO_RX_EMPTY) != 0u) {
        return NRF24_OK;
    }

    if (handle->dynamicPayloadEnabled) {
        status = execute_command(handle, CMD_R_RX_PL_WID, NULL, &width, 1u, NULL);
        if (status != NRF24_OK) return status;
        if (width == 0u || width > NRF24_MAX_PAYLOAD_SIZE) {
            (void)NRF24_FlushRx(handle);
            (void)NRF24_ClearIrqFlags(handle);
            return NRF24_INVALID_PAYLOAD_WIDTH;
        }
    } else {
        width = handle->payloadSize;
    }

    if (width > bufferSize) {
        (void)NRF24_FlushRx(handle);
        (void)NRF24_ClearIrqFlags(handle);
        return NRF24_INVALID_ARGUMENT;
    }
    status = execute_command(handle, CMD_R_RX_PAYLOAD, NULL, data, width, NULL);
    if (status != NRF24_OK) return status;
    *receivedLength = width;
    return NRF24_WriteRegister(handle, REG_STATUS, BIT_MASK_RX_DR);
}

/** Zwraca nazwę kodu statusu do komunikatów diagnostycznych lub NRF24_UNKNOWN dla nieznanego kodu. */
const char *NRF24_StatusToString(NRF24_Status_t status)
{
    switch (status) {
    case NRF24_OK: return "NRF24_OK";
    case NRF24_ERROR: return "NRF24_ERROR";
    case NRF24_TIMEOUT: return "NRF24_TIMEOUT";
    case NRF24_MAX_RETRY: return "NRF24_MAX_RETRY";
    case NRF24_INVALID_ARGUMENT: return "NRF24_INVALID_ARGUMENT";
    case NRF24_INVALID_PAYLOAD_WIDTH: return "NRF24_INVALID_PAYLOAD_WIDTH";
    default: return "NRF24_UNKNOWN";
    }
}
