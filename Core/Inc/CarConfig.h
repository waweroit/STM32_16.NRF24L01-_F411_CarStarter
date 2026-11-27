#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* Hardware assumptions: active-high relay drivers; PA1 switches the nRF24L01
 * supply, DS18B20 externally powered (not parasite), external button pulldown. */
#define CAR_ROOT_INDEX             0u
#define CAR_DEVICE_INDEX           2u /* device 0x03, address ending C3 */
#define CAR_LISTEN_MS              30000u
#define CAR_WAKE_SETTLE_MS         500u /* supply settling after MCU STOP */
#define CAR_RADIO_SETTLE_MS        200u /* supply/capacitor settling after radio power-on */
#define CAR_SLEEP_SECONDS          60u
#define CAR_REPORT_MS              60000u /* while awake with engine/timer active */
#define CAR_DEFAULT_STOP_SECONDS   900u
#define CAR_MIN_STOP_SECONDS       30u
#define CAR_MAX_STOP_SECONDS       3600u
#define CAR_KEY_SETTLE_MS          200u
#define CAR_BUTTON_PULSE_MS        2200u
#define CAR_STOP_SETTLE_MS         5000u
#define CAR_STOP_VERIFY_MS         15000u
#define CAR_START_OBSERVE_MS       15000u
#define CAR_DOUBLE_STOP_MS         20000u
#define CAR_ADC_SAMPLE_COUNT       32u
#define CAR_ADC_SETTLE_MS          5u
#define CAR_TEMP_POWER_SETTLE_MS   10u
#define CAR_ADC_VDDA_MV            3287u
#define CAR_ADC_DIVIDER_MILLI      4917u /* (470 + 120) / 120, scaled by 1000 */
#define CAR_DEFAULT_RUNNING_MV     10330u
#define CAR_DEFAULT_STOPPED_MV     9320u
/* Tymczasowy test: 1 używa progów z kodu przy starcie, 0 odczytuje je z W25Q64. */
#define CAR_USE_DEFAULT_THRESHOLDS_AT_BOOT 0
/* No new minimum battery voltage is imposed without a hardware decision. */
#define CAR_MIN_START_MV           0u
/* Set to 1 locally when USB should prevent STOP during bench diagnostics. */
#define CAR_USB_KEEP_AWAKE         0
#endif
