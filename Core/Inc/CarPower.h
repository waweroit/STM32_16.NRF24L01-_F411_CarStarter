#ifndef CAR_POWER_H
#define CAR_POWER_H
#include "W25Q64.h"
typedef enum { CAR_SLEEP_CANCELLED, CAR_SLEEP_COMPLETED, CAR_FLASH_WAKE_ERROR } CarSleepResult;
CarSleepResult CarPower_Sleep(W25Q_HandleTypeDef *flash);
bool CarPower_UsbActive(void);
#endif
