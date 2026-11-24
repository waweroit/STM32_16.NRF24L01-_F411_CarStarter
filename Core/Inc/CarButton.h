#ifndef CAR_BUTTON_H
#define CAR_BUTTON_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { CAR_CLICK_NONE, CAR_CLICK_SINGLE, CAR_CLICK_DOUBLE, CAR_CLICK_LONG } CarClick;
void CarButton_Init(void);
CarClick CarButton_Process(uint32_t now);
/* Timestamp of the last returned gesture; double click uses its rising edge. */
uint32_t CarButton_EventAt(void);
bool CarButton_Busy(void);
bool CarButton_WakePending(void);
#endif
