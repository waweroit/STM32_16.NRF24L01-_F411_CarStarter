#include "CarButton.h"
#include "main.h"
#define EDGE_COUNT 32u
typedef struct { uint32_t at; bool level; } ButtonEdge;
static volatile ButtonEdge edges[EDGE_COUNT];
static volatile unsigned head, tail;
static volatile bool overflow;
static bool raw, stable, waiting, longSent, second;
static uint32_t changedAt, pressedAt, releasedAt, eventAt;

/* Both edges are captured even while the foreground restores peripherals.
 * HAL ticks resume immediately after STOP; no gesture spans sleep (Busy guard). */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin != ResetUserButton_EXTI_Pin) return;
    unsigned next = (head + 1u) % EDGE_COUNT;
    if (next == tail) { overflow = true; return; }
    edges[head].at = HAL_GetTick();
    edges[head].level = HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, pin) == GPIO_PIN_SET;
    __DMB();
    head = next;
}
void CarButton_Init(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    head = tail = 0u; overflow = false;
    raw = stable = HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, ResetUserButton_EXTI_Pin) == GPIO_PIN_SET;
    waiting = longSent = second = false;
    changedAt = pressedAt = eventAt = HAL_GetTick(); releasedAt = 0u;
    __set_PRIMASK(mask);
}
/* Advance the recognizer to an edge timestamp before applying that edge.
 * Return one gesture at a time so a backlog cannot overwrite earlier gestures. */
static CarClick Advance(uint32_t now)
{
    if (stable != raw && now - changedAt >= 20u) {
        uint32_t at = changedAt + 20u;
        if (raw && waiting && at - releasedAt > 1000u) {
            waiting = false; eventAt = releasedAt + 1001u;
            return CAR_CLICK_SINGLE;
        }
        stable = raw;
        if (stable) {
            pressedAt = at; longSent = false;
            second = waiting && at - releasedAt <= 1000u;
            if (second) { waiting = false; eventAt = changedAt; return CAR_CLICK_DOUBLE; }
        } else if (!longSent && !second) {
            releasedAt = at; waiting = true;
        } else second = false;
    }
    if (stable && !longSent && !second && now - pressedAt >= 1000u) {
        longSent = true; waiting = false; eventAt = pressedAt + 1000u;
        return CAR_CLICK_LONG;
    }
    if (!stable && waiting && now - releasedAt > 1000u) {
        waiting = false; eventAt = releasedAt + 1001u;
        return CAR_CLICK_SINGLE;
    }
    return CAR_CLICK_NONE;
}
CarClick CarButton_Process(uint32_t now)
{
    for (;;) {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (overflow) {
            /* Lost history must never synthesize a double click. Discard it
             * and suppress any held press until release. */
            tail = head; overflow = false;
            raw = stable = HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, ResetUserButton_EXTI_Pin) == GPIO_PIN_SET;
            waiting = second = false; longSent = true; changedAt = now;
            __set_PRIMASK(mask);
            return CAR_CLICK_NONE;
        }
        bool available = tail != head;
        ButtonEdge edge = {0u, false};
        if (available) { edge.at = edges[tail].at; edge.level = edges[tail].level; }
        __set_PRIMASK(mask);
        /* An interrupt can be newer than the caller's snapshot of HAL time. */
        if (!available || (int32_t)(now - edge.at) < 0) return Advance(now);
        CarClick click = Advance(edge.at);
        if (click != CAR_CLICK_NONE) return click;
        if (edge.level != raw) { raw = edge.level; changedAt = edge.at; }
        mask = __get_PRIMASK(); __disable_irq();
        tail = (tail + 1u) % EDGE_COUNT;
        __set_PRIMASK(mask);
    }
}
uint32_t CarButton_EventAt(void) { return eventAt; }
bool CarButton_Busy(void) { return raw || stable || waiting || CarButton_WakePending(); }
bool CarButton_WakePending(void) { return head != tail || overflow; }
