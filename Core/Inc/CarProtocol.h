#ifndef CAR_PROTOCOL_H
#define CAR_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#define CAR_MSG_GET_TEMPERATURE 0x20u
#define CAR_MSG_GET_VOLTAGE     0x21u
#define CAR_MSG_GET_STATUS      0x22u
#define CAR_MSG_START           0x23u
#define CAR_MSG_SET_TIMEOUT     0x24u
#define CAR_MSG_GET_TIMEOUT     0x25u
#define CAR_MSG_STOP            0x26u
#define CAR_MSG_SET_DEBUG       0x27u
#define CAR_MSG_READY           0x28u
#define CAR_MSG_TELEMETRY       0x29u
#define CAR_MSG_RESULT          0x2Au
#define CAR_MSG_TIMEOUT         0x2Bu
#define CAR_MSG_SET_THRESHOLDS  0x2Cu
#define CAR_MSG_GET_THRESHOLDS  0x2Du
#define CAR_MSG_THRESHOLDS      0x2Eu
#define CAR_MAX_THRESHOLD_MV    16000u
#define CAR_ENGINE_RESULT_MS    25000u
#define CAR_START_MAX_MINUTES   30u

static inline bool Car_ThresholdsValid(uint16_t runningMv, uint16_t stoppedMv)
{
    return stoppedMv > 0u && stoppedMv < runningMv && runningMv <= CAR_MAX_THRESHOLD_MV;
}

/* Wire format always big endian. No raw C structs or floats on the radio. */
/** Odczytuje 16-bitową liczbę z dwóch bajtów bufora p w kolejności sieciowej. */
static inline uint16_t Car_Get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8)|p[1]; }
/** Odczytuje 32-bitową liczbę z czterech bajtów bufora p w kolejności sieciowej. */
static inline uint32_t Car_Get32(const uint8_t *p) { return ((uint32_t)Car_Get16(p)<<16)|Car_Get16(p+2); }
/** Zapisuje wartość v do dwóch bajtów bufora p, zaczynając od starszego bajtu. */
static inline void Car_Put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
/** Zapisuje wartość v do czterech bajtów bufora p, zaczynając od najstarszego bajtu. */
static inline void Car_Put32(uint8_t *p, uint32_t v) { Car_Put16(p,(uint16_t)(v>>16)); Car_Put16(p+2,(uint16_t)v); }
#endif
