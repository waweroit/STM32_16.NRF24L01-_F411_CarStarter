#include "CarSensors.h"
#include "adc.h"
#include "tim.h"
#include "ow.h"
#include <limits.h>

static TIM_TypeDef timer;
TIM_HandleTypeDef htim1 = {.Instance=&timer};
ADC_HandleTypeDef hadc1;
static uint32_t tick;
static bool power, timerRunning, failRegister, failStart;
static void (*timerCallback)(TIM_HandleTypeDef *);

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t ms) { tick+=ms; }
void RadioTestPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    RadioTestPin(port,pin,level);
    if (port==TempMesMosfet_GPIO_Port && pin==TempMesMosfet_Pin) power=(level==GPIO_PIN_SET);
}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h, uint32_t *p, uint32_t n)
{ (void)h; (void)p; (void)n; return HAL_ERROR; }
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_RegisterCallback(TIM_HandleTypeDef *h, HAL_TIM_CallbackIDTypeDef id,
                                           pTIM_CallbackTypeDef callback)
{
    if (failRegister || h!=&htim1 || id!=HAL_TIM_PERIOD_ELAPSED_CB_ID) return HAL_ERROR;
    timerCallback=callback;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h)
{
    if (failStart || !power || !timerCallback) return HAL_ERROR;
    timerRunning=true;
    timerCallback(h); /* First IRQ arrives before this function returns. */
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *h)
{ (void)h; timerRunning=false; return HAL_OK; }

static void advance(uint32_t ms) { tick+=ms; CarSensors_Process(tick); }
static void wireLevel(bool high)
{
    DS1820_TempSensor_GPIO_Port->IDR=high ? DS1820_TempSensor_Pin : 0;
}
static void pulse(bool high) { wireLevel(high); timerCallback(&htim1); }
/* Simulate presence, 16 command slots, and optionally nine scratchpad bytes. */
static void transfer(const uint8_t *bytes)
{
    pulse(true);
    pulse(false);
    for (unsigned i=0; i<32; ++i) pulse(true);
    if (bytes) {
        for (unsigned i=0; i<72; ++i) {
            pulse(true); pulse(true); pulse((bytes[i/8]>>(i%8))&1u);
        }
    }
    pulse(true);
}

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int SensorTests(void)
{
    uint8_t bytes[]={0x98,0x01,75,70,0x7f,0xff,0x0c,0x10,0}; /* +25.5 C */
    bytes[8]=ow_crc(bytes,8);
    wireLevel(true);
    tick=UINT32_MAX-20u;
    CarSensors_Init();
    for (unsigned attempt=0; attempt<7; ++attempt) {
        CHECK(power && !CarSensors_TemperatureReady());
        advance(9); CHECK(!timerRunning);
        failRegister=attempt==1;
        failStart=attempt==2;
        advance(1);
        if (!failRegister && !failStart) {
            CHECK(timerRunning);
            if (attempt==3) {
                pulse(true); pulse(true); /* no presence */
                advance(1);
            } else {
                transfer(NULL);
                CHECK(!timerRunning);
                advance(35); /* delayed observation of the completed command */
                advance(799); CHECK(!timerRunning);
                advance(1); CHECK(timerRunning);
                if (attempt==4) bytes[8]^=1;
                uint8_t zeros[9]={0};
                transfer(attempt==5 ? zeros : bytes);
                if (attempt==4) bytes[8]^=1;
                advance(2);
            }
        }
        CHECK(!power && !timerRunning && CarSensors_TemperatureReady());
        CHECK(CarSensors_Temperature()==((attempt==0 || attempt==6) ? 2550 : INT16_MIN));
        if (attempt==0) CarSensors_Suspend();
        if (attempt<6) CarSensors_RequestTemperature();
    }
    return 0;
}
