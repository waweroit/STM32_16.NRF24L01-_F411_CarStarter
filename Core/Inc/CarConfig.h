#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* Hardware assumptions: active-high relay drivers; PA1 switches the peripheral
 * supply, DS18B20 externally powered (not parasite), external button pulldown. */
#define CAR_ROOT_INDEX             0u
#define CAR_DEVICE_INDEX           2u /* device 0x03, address ending C3 */
#define CAR_LISTEN_MS              30000u
#define CAR_SLEEP_SECONDS          60u
#define CAR_REPORT_MS              60000u /* while awake with engine/timer active */
#define CAR_DEFAULT_STOP_SECONDS   900u
#define CAR_MIN_STOP_SECONDS       30u
#define CAR_MAX_STOP_SECONDS       3600u
#define CAR_KEY_SETTLE_MS          200u
#define CAR_BUTTON_PULSE_MS        2200u
#define CAR_START_OBSERVE_MS       15000u
#define CAR_DOUBLE_STOP_MS         20000u
#define CAR_ADC_SAMPLE_COUNT       32u
#define CAR_ADC_VDDA_MV            3287u
#define CAR_ADC_DIVIDER_MILLI      5726u
#define CAR_ENGINE_RUNNING_MV      12900u
#define CAR_ENGINE_STOPPED_MV      12600u
#define CAR_START_EARLY_MV         13400u
#define CAR_START_RISE_MV          400u
/* No new minimum battery voltage is imposed without a hardware decision. */
#define CAR_MIN_START_MV           0u
/* Keep USB configured sessions awake for diagnostics. */
#define CAR_USB_KEEP_AWAKE         1
#endif
