#ifndef CAR_SENSORS_H
#define CAR_SENSORS_H
#include <stdbool.h>
#include <stdint.h>
void CarSensors_Init(void);
void CarSensors_Process(uint32_t now);
void CarSensors_RequestTemperature(void);
bool CarSensors_Busy(void);
bool CarSensors_VoltageValid(uint32_t now);
uint16_t CarSensors_Millivolts(void);
int16_t CarSensors_Temperature(void); /* centidegrees; INT16_MIN = invalid */
bool CarSensors_TemperatureReady(void);
void CarSensors_Suspend(void);
#endif
