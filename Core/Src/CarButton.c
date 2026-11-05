#include "CarButton.h"
#include "main.h"
static volatile bool wakePending;
static bool raw, stable, waiting, longSent, second;
static uint32_t changedAt, pressedAt, releasedAt;
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == ResetUserButton_EXTI_Pin) wakePending = true;
}
void CarButton_Init(void)
{
    raw = stable = HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, ResetUserButton_EXTI_Pin) == GPIO_PIN_SET;
    changedAt = pressedAt = HAL_GetTick();
}
CarClick CarButton_Process(uint32_t now)
{
    wakePending = false;
    bool level = HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, ResetUserButton_EXTI_Pin) == GPIO_PIN_SET;
    if (level != raw) { raw = level; changedAt = now; }
    if (stable != raw && now-changedAt >= 20u) {
        stable = raw;
        if (stable) {
            pressedAt = now; longSent = false;
            second = waiting && now-releasedAt <= 1000u;
            if (second) { waiting = false; return CAR_CLICK_DOUBLE; }
        } else if (!longSent && !second) {
            releasedAt = now; waiting = true;
        } else { second = false; }
    }
    if (stable && !longSent && !second && now-pressedAt >= 1000u) {
        longSent = true; waiting = false; return CAR_CLICK_LONG;
    }
    if (!stable && waiting && now-releasedAt > 1000u) {
        waiting = false; return CAR_CLICK_SINGLE;
    }
    return CAR_CLICK_NONE;
}
bool CarButton_Busy(void) { return raw || stable || waiting; }
bool CarButton_WakePending(void) { return wakePending; }
