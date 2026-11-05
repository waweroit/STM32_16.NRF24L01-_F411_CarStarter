#include "CarSensors.h"
#include "CarConfig.h"
#include "adc.h"
#include "tim.h"
#include "ds18b20.h"
#include <limits.h>
#include <string.h>
static uint16_t adcBuffer[CAR_ADC_SAMPLE_COUNT] __attribute__((aligned(4)));
static volatile bool adcDone, adcError;
static bool adcBusy, voltageValid;
static uint16_t millivolts;
static uint32_t adcAt, voltageAt, lastAdc;
static ds18b20_t sensor;
static enum { TEMP_IDLE, TEMP_CONVERT, TEMP_READ } tempState;
static uint32_t tempAt;
static int16_t temperature = INT16_MIN;
static bool tempReady;
static void OneWireTick(TIM_HandleTypeDef *timer)
{
    if (timer == &htim1) ow_callback(&sensor.ow);
}
void CarSensors_Init(void)
{
    memset(&sensor, 0, sizeof(sensor));
    ow_init_t init = {.tim_handle=&htim1, .tim_cb=OneWireTick,
        .gpio=DS1820_TempSensor_GPIO_Port, .pin=DS1820_TempSensor_Pin};
    ds18b20_init(&sensor, &init);
    lastAdc = HAL_GetTick() - 1000u;
    CarSensors_RequestTemperature();
}
void CarSensors_RequestTemperature(void)
{
    if (tempState != TEMP_IDLE) return;
    tempReady = false; temperature = INT16_MIN; tempAt = HAL_GetTick();
    if (ds18b20_cnv(&sensor) == OW_ERR_NONE) tempState = TEMP_CONVERT;
    else tempReady = true;
}
void CarSensors_Process(uint32_t now)
{
    if (adcBusy && (adcDone || adcError || now-adcAt >= 100u)) {
        bool success = adcDone && !adcError;
        HAL_ADC_Stop_DMA(&hadc1); adcBusy = false;
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
    if (!adcBusy && now-lastAdc >= 1000u) {
        lastAdc = adcAt = now; adcDone = adcError = false;
        adcBusy = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuffer, CAR_ADC_SAMPLE_COUNT) == HAL_OK;
        if (!adcBusy) voltageValid = false;
    }
    if (tempState != TEMP_IDLE && now-tempAt > 1600u) {
        HAL_TIM_Base_Stop_IT(&htim1);
        sensor.ow.state = OW_STATE_IDLE;
        HAL_GPIO_WritePin(DS1820_TempSensor_GPIO_Port, DS1820_TempSensor_Pin, GPIO_PIN_SET);
        temperature = INT16_MIN; tempReady = true; tempState = TEMP_IDLE;
    }
    if (tempState == TEMP_CONVERT && !ds18b20_is_busy(&sensor)) {
        if (ds18b20_last_error(&sensor) != OW_ERR_NONE) {
            tempState = TEMP_IDLE; tempReady = true;
        } else if (ds18b20_is_cnv_done(&sensor)) {
            if (ds18b20_req_read(&sensor) == OW_ERR_NONE) tempState = TEMP_READ;
            else { tempState = TEMP_IDLE; tempReady = true; }
        }
    } else if (tempState == TEMP_READ && !ds18b20_is_busy(&sensor)) {
        int16_t value = ds18b20_last_error(&sensor) == OW_ERR_NONE ? ds18b20_read_c(&sensor) : -10000;
        temperature = value < -5500 || value > 12500 ? INT16_MIN : value;
        tempState = TEMP_IDLE; tempReady = true;
    }
}
bool CarSensors_Busy(void) { return adcBusy || tempState != TEMP_IDLE; }
bool CarSensors_VoltageValid(uint32_t now) { return voltageValid && now-voltageAt < 2500u; }
uint16_t CarSensors_Millivolts(void) { return millivolts; }
int16_t CarSensors_Temperature(void) { return temperature; }
bool CarSensors_TemperatureReady(void) { return tempReady; }
void CarSensors_Suspend(void)
{
    HAL_ADC_Stop_DMA(&hadc1); adcBusy = false; voltageValid = false;
    HAL_TIM_Base_Stop_IT(&htim1);
    temperature = INT16_MIN; tempState = TEMP_IDLE;
    lastAdc = HAL_GetTick()-1000u;
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1) {
        __HAL_ADC_DISABLE(adc); /* Stop continuous conversion immediately. */
        adcDone = true;
    }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1) { __HAL_ADC_DISABLE(adc); adcError = true; }
}
