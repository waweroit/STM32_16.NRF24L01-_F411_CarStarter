#ifndef CAR_ENGINE_H
#define CAR_ENGINE_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { CAR_ENGINE_UNKNOWN, CAR_ENGINE_STOPPED, CAR_ENGINE_RUNNING } CarEngineStatus;
typedef enum { CAR_IDLE, CAR_KEY_WAIT, CAR_START_PULSE, CAR_START_OBSERVE,
               CAR_STOP_PULSE, CAR_STOP_VERIFY } CarEnginePhase;
typedef enum { CAR_RESULT_ACCEPTED, CAR_RESULT_STARTED, CAR_RESULT_START_FAILED,
    CAR_RESULT_STOPPED, CAR_RESULT_STOP_FAILED, CAR_RESULT_BUSY,
    CAR_RESULT_ALREADY_RUNNING, CAR_RESULT_BAD_REQUEST, CAR_RESULT_STORAGE_ERROR,
    CAR_RESULT_STALE_WINDOW, CAR_RESULT_DUPLICATE, CAR_RESULT_SENSOR_ERROR,
    CAR_RESULT_TIMEOUT_SAVED, CAR_RESULT_TIMER_CANCELLED } CarResult;
typedef struct {
    void (*key)(bool on);
    void (*button)(bool on);
    void (*result)(CarResult result, uint32_t request);
} CarEngineIo;
typedef struct {
    CarEngineIo io;
    CarEngineStatus status;
    CarEnginePhase phase;
    uint32_t phaseAt, startedAt, buttonStopAt, timeoutSeconds, request;
    uint16_t beforeMv;
    bool autoStop, buttonStop, takeover;
} CarEngine;
void CarEngine_Init(CarEngine *e, const CarEngineIo *io, uint32_t seconds);
CarResult CarEngine_Start(CarEngine *e, uint32_t now, uint16_t mv, bool valid, uint32_t request);
CarResult CarEngine_Stop(CarEngine *e, uint32_t now, bool valid);
void CarEngine_Process(CarEngine *e, uint32_t now, uint16_t mv, bool valid);
void CarEngine_SingleClick(CarEngine *e);
void CarEngine_DoubleClick(CarEngine *e, uint32_t now);
void CarEngine_SetTimeout(CarEngine *e, uint32_t seconds);
uint16_t CarEngine_Remaining(const CarEngine *e, uint32_t now);
bool CarEngine_NeedsAwake(const CarEngine *e);
#endif
