#ifndef CAR_POWER_H
#define CAR_POWER_H
#include "W25Q64.h"
#include "NrfLink.h"
bool CarPower_Sleep(W25Q_HandleTypeDef *flash, NrfLink_t *radio, bool radioReady);
bool CarPower_UsbActive(void);
#endif
