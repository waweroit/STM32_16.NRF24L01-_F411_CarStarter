#include "CarSensors.h"
#include "CarConfig.h"
#include "adc.h"
#include "tim.h"
#include "ds18b20.h"
#include <limits.h>
#include <string.h>
static uint16_t adcBuffer[CAR_ADC_SAMPLE_COUNT] __attribute__((aligned(4)));
static volatile bool adcDone, adcError;
static enum { ADC_IDLE, ADC_SETTLE, ADC_SAMPLING } adcState;
static bool voltageValid;
static uint16_t millivolts;
static uint32_t adcAt, voltageAt, lastAdc;
static ds18b20_t sensor;
static enum { TEMP_IDLE, TEMP_POWER_UP, TEMP_COMMAND, TEMP_CONVERT, TEMP_READ } tempState;
static uint32_t tempAt;
static int16_t temperature = INT16_MIN;
static bool tempReady;
/** Obsługuje kolejny krok transmisji 1-Wire, gdy timer przekazany przez HAL to TIM1. */
static void OneWireTick(TIM_HandleTypeDef *timer)
{
    if (timer == &htim1) ow_callback(&sensor.ow);
}
static bool InitTemperatureBus(void)
{
    memset(&sensor, 0, sizeof(sensor));
    ow_init_t init = {.tim_handle=&htim1, .tim_cb=OneWireTick,
        .gpio=DS1820_TempSensor_GPIO_Port, .pin=DS1820_TempSensor_Pin};
    return ds18b20_init(&sensor, &init);
}
/** Konfiguruje magistralę czujnika temperatury, zleca pierwszą konwersję i przygotowuje
 * pierwszy pomiar ADC. */
void CarSensors_Init(void)
{
    lastAdc = HAL_GetTick() - 1000u;
    CarSensors_RequestTemperature();
}
/** Rozpoczyna konwersję DS18B20, jeśli poprzedni cykl pomiarowy już się zakończył. */
void CarSensors_RequestTemperature(void)
{
    if (tempState != TEMP_IDLE) return;
    tempReady = false;
    temperature = INT16_MIN;
    HAL_GPIO_WritePin(TempMesMosfet_GPIO_Port, TempMesMosfet_Pin, GPIO_PIN_SET);
    tempAt = HAL_GetTick();
    tempState = TEMP_POWER_UP;
}

static void FinishTemperature(void)
{
    HAL_GPIO_WritePin(TempMesMosfet_GPIO_Port, TempMesMosfet_Pin, GPIO_PIN_RESET);
    tempState = TEMP_IDLE;
    tempReady = true;
}
/** Obsługuje próbki ADC i kolejne etapy odczytu DS18B20. now to bieżący czas HAL w ms,
 * używany do planowania pomiarów i wykrywania przekroczenia czasu. */
void CarSensors_Process(uint32_t now)
{
    if (adcState == ADC_SAMPLING && (adcDone || adcError || now-adcAt >= 100u)) {
        bool success = adcDone && !adcError;
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_GPIO_WritePin(VoltMesMosfet_GPIO_Port, VoltMesMosfet_Pin, GPIO_PIN_RESET);
        adcState = ADC_IDLE;
        voltageValid = success;
        if (success) {
            uint32_t sum = 0u;
            for (unsigned i=0; i<CAR_ADC_SAMPLE_COUNT; ++i) sum += adcBuffer[i];
            uint32_t mean = (sum + CAR_ADC_SAMPLE_COUNT/2u)/CAR_ADC_SAMPLE_COUNT;
            uint64_t scaled = (uint64_t)mean * CAR_ADC_VDDA_MV * CAR_ADC_DIVIDER_MILLI;
            millivolts = (uint16_t)((scaled + 2047500u)/4095000u);
            /* Rail readings/open divider are invalid, not a stopped engine. */
            voltageValid = mean > 10u && mean < 4090u;
            voltageAt = now;
        }
    }
    if (adcState == ADC_IDLE && now-lastAdc >= 1000u) {
        HAL_GPIO_WritePin(VoltMesMosfet_GPIO_Port, VoltMesMosfet_Pin, GPIO_PIN_SET);
        lastAdc = HAL_GetTick();
        adcState = ADC_SETTLE;
    } else if (adcState == ADC_SETTLE && now-lastAdc >= CAR_ADC_SETTLE_MS) {
        adcDone = adcError = false;
        adcAt = now;
        if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuffer, CAR_ADC_SAMPLE_COUNT) == HAL_OK) {
            adcState = ADC_SAMPLING;
        } else {
            HAL_ADC_Stop_DMA(&hadc1);
            HAL_GPIO_WritePin(VoltMesMosfet_GPIO_Port, VoltMesMosfet_Pin, GPIO_PIN_RESET);
            voltageValid = false;
            adcState = ADC_IDLE;
        }
    }
    if (tempState != TEMP_IDLE && now-tempAt > 1600u) {
        HAL_TIM_Base_Stop_IT(&htim1);
        sensor.ow.state = OW_STATE_IDLE;
        HAL_GPIO_WritePin(DS1820_TempSensor_GPIO_Port, DS1820_TempSensor_Pin, GPIO_PIN_SET);
        FinishTemperature();
    } else if (tempState == TEMP_POWER_UP && now-tempAt >= CAR_TEMP_POWER_SETTLE_MS) {
        if (InitTemperatureBus() && ds18b20_cnv(&sensor) == OW_ERR_NONE) tempState = TEMP_COMMAND;
        else FinishTemperature();
    } else if (tempState == TEMP_COMMAND && !ds18b20_is_busy(&sensor)) {
        if (ds18b20_last_error(&sensor) != OW_ERR_NONE) {
            FinishTemperature();
        } else {
            /* The conversion starts after the command has reached the sensor. */
            tempAt = now;
            tempState = TEMP_CONVERT;
        }
    } else if (tempState == TEMP_CONVERT && now-tempAt >= sensor.cnv_time) {
        if (ds18b20_req_read(&sensor) == OW_ERR_NONE) tempState = TEMP_READ;
        else FinishTemperature();
    } else if (tempState == TEMP_READ && !ds18b20_is_busy(&sensor)) {
        int16_t value = ds18b20_last_error(&sensor) == OW_ERR_NONE ? ds18b20_read_c(&sensor) : -10000;
        temperature = value < -5500 || value > 12500 ? INT16_MIN : value;
        FinishTemperature();
    }
}
/** Zwraca true, gdy trwa pobieranie próbek ADC lub pomiar temperatury. */
bool CarSensors_Busy(void) { return adcState != ADC_IDLE || tempState != TEMP_IDLE; }
/** Sprawdza poprawność ostatniego pomiaru napięcia i jego wiek względem czasu now (ms). */
bool CarSensors_VoltageValid(uint32_t now) { return voltageValid && now-voltageAt < 2500u; }
/** Zwraca zapamiętane napięcie w mV. Jego przydatność do decyzji o silniku określa
 * CarSensors_VoltageValid(). */
uint16_t CarSensors_Millivolts(void) { return millivolts; }
/** Zwraca temperaturę w setnych częściach °C lub INT16_MIN, gdy brak poprawnego odczytu. */
int16_t CarSensors_Temperature(void) { return temperature; }
/** Zwraca true po zakończeniu próby pomiaru temperatury, również w przypadku błędu. */
bool CarSensors_TemperatureReady(void) { return tempReady; }
/** Zatrzymuje ADC i zegar 1-Wire przed uśpieniem oraz unieważnia zapisane pomiary. */
void CarSensors_Suspend(void)
{
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_GPIO_WritePin(VoltMesMosfet_GPIO_Port, VoltMesMosfet_Pin, GPIO_PIN_RESET);
    adcState = ADC_IDLE;
    voltageValid = false;
    HAL_TIM_Base_Stop_IT(&htim1);
    sensor.ow.state = OW_STATE_IDLE;
    HAL_GPIO_WritePin(DS1820_TempSensor_GPIO_Port, DS1820_TempSensor_Pin, GPIO_PIN_SET);
    temperature = INT16_MIN;
    FinishTemperature();
    lastAdc = HAL_GetTick()-1000u;
}
/** Dla uchwytu adc odpowiadającego ADC1 zatrzymuje konwersję i zgłasza pętli głównej gotową
 * serię próbek. */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1) {
        __HAL_ADC_DISABLE(adc); /* Stop continuous conversion immediately. */
        adcDone = true;
    }
}
/** Dla uchwytu adc odpowiadającego ADC1 zatrzymuje konwersję i zgłasza błąd pętli głównej. */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1) { __HAL_ADC_DISABLE(adc); adcError = true; }
}
