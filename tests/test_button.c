#include "CarButton.h"
#include "main.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static GPIO_PinState level;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ (void)port; (void)pin; return level; }
static void edge(uint32_t at, bool high)
{
    HAL_Delay(at - HAL_GetTick());
    level = high ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_EXTI_Callback(ResetUserButton_EXTI_Pin);
}
static void reset(uint32_t at)
{
    HAL_Delay(at - HAL_GetTick()); level=GPIO_PIN_RESET; CarButton_Init();
}
int ButtonTests(void)
{
    /* Entire quick double click happens during the 500 ms wake delay. */
    reset(0u);
    edge(10u,true); edge(80u,false); edge(170u,true); edge(240u,false);
    CHECK(CarButton_WakePending());
    CHECK(CarButton_Process(510u)==CAR_CLICK_DOUBLE);
    CHECK(CarButton_EventAt()==170u);
    CHECK(CarButton_Process(510u)==CAR_CLICK_NONE);
    CHECK(!CarButton_Busy());
    CHECK(CarButton_Process(2000u)==CAR_CLICK_NONE);
    /* Bounce must not synthesize another click. */
    reset(3000u);
    edge(3010u,true); edge(3013u,false); edge(3016u,true);
    edge(3100u,false); edge(3104u,true); edge(3108u,false);
    CHECK(CarButton_Process(3500u)==CAR_CLICK_NONE);
    CHECK(CarButton_Process(4129u)==CAR_CLICK_SINGLE);
    CHECK(CarButton_Process(4200u)==CAR_CLICK_NONE);
    /* Long held press, including release, replayed after a foreground stall. */
    reset(5000u); edge(5010u,true); edge(6500u,false);
    CHECK(CarButton_Process(7000u)==CAR_CLICK_LONG);
    CHECK(CarButton_Process(8000u)==CAR_CLICK_NONE);
    /* Two clicks separated by more than the window remain two singles. */
    reset(9000u); edge(9010u,true); edge(9080u,false);
    edge(10200u,true); edge(10300u,false);
    CHECK(CarButton_Process(12000u)==CAR_CLICK_SINGLE);
    CHECK(CarButton_Process(12000u)==CAR_CLICK_SINGLE);
    CHECK(CarButton_Process(12000u)==CAR_CLICK_NONE);
    /* Overflow discards ambiguous history rather than triggering shutdown. */
    reset(13000u);
    for (unsigned i=0;i<40u;++i) edge(13010u+i*2u,(i&1u)==0u);
    CHECK(CarButton_Process(14000u)==CAR_CLICK_NONE);
    CHECK(CarButton_Process(16000u)==CAR_CLICK_NONE);
    CHECK(!CarButton_Busy());
    /* Unsigned time differences survive HAL tick rollover. */
    reset(UINT32_MAX-200u);
    edge(UINT32_MAX-190u,true); edge(UINT32_MAX-120u,false);
    edge(20u,true); edge(90u,false);
    CHECK(CarButton_Process(400u)==CAR_CLICK_DOUBLE);
    CHECK(CarButton_EventAt()==20u);
    CHECK(CarButton_Process(400u)==CAR_CLICK_NONE);
    CHECK(!CarButton_Busy());
    return 0;
}
