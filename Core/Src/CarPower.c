#include "CarPower.h"
#include "CarConfig.h"
#include "CarButton.h"
#include "CarSensors.h"
#include "rtc.h"
#include "usb_device.h"
#include "usbd_def.h"
#include "usbd_core.h"
extern USBD_HandleTypeDef hUsbDeviceFS;
extern void SystemClock_Config(void);
bool CarPower_UsbActive(void)
{
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED;
}
bool CarPower_Sleep(W25Q_HandleTypeDef *flash, NrfLink_t *radio, bool radioReady)
{
    if (CarButton_Busy() || CarButton_WakePending()) return false;
    if (radioReady) {
        uint8_t config;
        HAL_GPIO_WritePin(NRF24_CE_GPIO_Port, NRF24_CE_Pin, GPIO_PIN_RESET);
        if (NRF24_ReadRegister(&radio->radio, 0u, &config) != NRF24_OK ||
            NRF24_WriteRegister(&radio->radio, 0u, config & ~2u) != NRF24_OK) return false;
    }
    if (!W25Q_PowerDown(flash)) return false;
    CarSensors_Suspend();
    if (USBD_DeInit(&hUsbDeviceFS) != USBD_OK) {
        MX_USB_DEVICE_Init(); W25Q_Wake(flash); return false;
    }
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
    __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();
    HAL_NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    bool armed = HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, CAR_SLEEP_SECONDS-1u,
                       RTC_WAKEUPCLOCK_CK_SPRE_16BITS) == HAL_OK;
    bool slept = false;
    if (armed) {
        HAL_SuspendTick();
        /* Mask interrupts across final button check and WFI: an edge between
         * these operations remains pending and makes WFI return immediately. */
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (!CarButton_WakePending() &&
            !__HAL_GPIO_EXTI_GET_IT(ResetUserButton_EXTI_Pin) &&
            HAL_GPIO_ReadPin(ResetUserButton_EXTI_GPIO_Port, ResetUserButton_EXTI_Pin) == GPIO_PIN_RESET) {
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
            __DSB();
            HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
            slept = true;
        }
        __set_PRIMASK(mask);
        HAL_ResumeTick();
        if (slept) SystemClock_Config();
    }
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
    __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();
    HAL_NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    MX_USB_DEVICE_Init();
    W25Q_Wake(flash);
    return slept;
}
