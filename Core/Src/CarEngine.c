#include "CarEngine.h"
#include "CarConfig.h"
#include <string.h>
void CarEngine_Init(CarEngine *e, const CarEngineIo *io, uint32_t seconds)
{
    memset(e, 0, sizeof(*e)); e->io = *io; e->timeoutSeconds = seconds;
    e->io.key(false); e->io.button(false);
}
CarResult CarEngine_Start(CarEngine *e, uint32_t now, uint16_t mv, bool valid, uint32_t request)
{
    if (e->phase != CAR_IDLE || e->buttonStop || e->autoStop) return CAR_RESULT_BUSY;
    if (!valid) return CAR_RESULT_SENSOR_ERROR;
    if (e->status != CAR_ENGINE_STOPPED || mv >= CAR_ENGINE_RUNNING_MV) return CAR_RESULT_ALREADY_RUNNING;
#if CAR_MIN_START_MV > 0
    if (mv < CAR_MIN_START_MV) return CAR_RESULT_SENSOR_ERROR;
#endif
    e->beforeMv = mv; e->request = request; e->takeover = false;
    e->phase = CAR_KEY_WAIT; e->phaseAt = now; e->io.key(true);
    return CAR_RESULT_ACCEPTED;
}
CarResult CarEngine_Stop(CarEngine *e, uint32_t now, bool valid)
{
    if (e->phase != CAR_IDLE) return CAR_RESULT_BUSY;
    e->autoStop = e->buttonStop = false;
    if (!valid || e->status == CAR_ENGINE_UNKNOWN) return CAR_RESULT_SENSOR_ERROR;
    if (e->status == CAR_ENGINE_STOPPED) return CAR_RESULT_STOPPED;
    e->io.key(false); e->io.button(true);
    e->phase = CAR_STOP_PULSE; e->phaseAt = now;
    return CAR_RESULT_ACCEPTED;
}
void CarEngine_Process(CarEngine *e, uint32_t now, uint16_t mv, bool valid)
{
    uint32_t elapsed = now - e->phaseAt;
    if (e->phase == CAR_KEY_WAIT && elapsed >= CAR_KEY_SETTLE_MS) {
        e->io.button(true); e->phase = CAR_START_PULSE; e->phaseAt = now;
    } else if (e->phase == CAR_START_PULSE && elapsed >= CAR_BUTTON_PULSE_MS) {
        e->io.button(false); e->phase = CAR_START_OBSERVE; e->phaseAt = now;
    } else if (e->phase == CAR_START_OBSERVE &&
               ((valid && mv > CAR_START_EARLY_MV) || elapsed >= CAR_START_OBSERVE_MS)) {
        e->io.key(false); e->phase = CAR_IDLE;
        bool success = valid && mv >= CAR_ENGINE_RUNNING_MV && (uint32_t)mv > (uint32_t)e->beforeMv + CAR_START_RISE_MV;
        bool charging = valid && mv >= CAR_ENGINE_RUNNING_MV;
        e->status = charging ? CAR_ENGINE_RUNNING : CAR_ENGINE_UNKNOWN;
        /* Even an inconclusive voltage rise must not leave an apparently
         * running remotely started engine without its shutdown countdown. */
        e->autoStop = charging && !e->takeover; e->startedAt = now;
        e->io.result(success ? CAR_RESULT_STARTED : CAR_RESULT_START_FAILED, e->request);
    } else if (e->phase == CAR_STOP_PULSE && elapsed >= CAR_BUTTON_PULSE_MS) {
        e->io.button(false); e->phase = CAR_STOP_VERIFY; e->phaseAt = now;
    } else if (e->phase == CAR_STOP_VERIFY && elapsed >= 5000u) {
        e->phase = CAR_IDLE;
        bool stopped = valid && mv < CAR_ENGINE_STOPPED_MV;
        e->status = stopped ? CAR_ENGINE_STOPPED : CAR_ENGINE_UNKNOWN;
        e->io.result(stopped ? CAR_RESULT_STOPPED : CAR_RESULT_STOP_FAILED, e->request);
    }
    if (e->phase != CAR_IDLE) return;
    if (!valid) e->status = CAR_ENGINE_UNKNOWN;
    else if (mv >= CAR_ENGINE_RUNNING_MV) e->status = CAR_ENGINE_RUNNING;
    else if (mv < CAR_ENGINE_STOPPED_MV) e->status = CAR_ENGINE_STOPPED;
    /* An externally stopped engine must never receive a delayed toggle pulse. */
    if (e->status == CAR_ENGINE_STOPPED) e->autoStop = e->buttonStop = false;
    bool expired = (e->autoStop && now - e->startedAt >= e->timeoutSeconds * 1000u) ||
                   (e->buttonStop && now - e->buttonStopAt >= CAR_DOUBLE_STOP_MS);
    if (expired) {
        CarResult r = CarEngine_Stop(e, now, valid);
        if (r != CAR_RESULT_ACCEPTED) e->io.result(r, e->request);
    }
}
void CarEngine_SingleClick(CarEngine *e)
{
    e->autoStop = false; e->takeover = true;
    e->io.result(CAR_RESULT_TIMER_CANCELLED, e->request);
}
void CarEngine_DoubleClick(CarEngine *e, uint32_t now)
{
    if (e->phase != CAR_IDLE || e->status != CAR_ENGINE_RUNNING) return;
    e->buttonStop = true; e->buttonStopAt = now;
}
void CarEngine_SetTimeout(CarEngine *e, uint32_t seconds) { e->timeoutSeconds = seconds; }
uint16_t CarEngine_Remaining(const CarEngine *e, uint32_t now)
{
    uint32_t ms = UINT32_MAX;
    if (e->autoStop) {
        uint32_t elapsed = now - e->startedAt, limit = e->timeoutSeconds * 1000u;
        ms = elapsed >= limit ? 0u : limit - elapsed;
    }
    if (e->buttonStop) {
        uint32_t elapsed = now - e->buttonStopAt;
        uint32_t other = elapsed >= CAR_DOUBLE_STOP_MS ? 0u : CAR_DOUBLE_STOP_MS - elapsed;
        if (other < ms) ms = other;
    }
    return ms == UINT32_MAX ? 0u : (uint16_t)((ms + 999u) / 1000u);
}
bool CarEngine_NeedsAwake(const CarEngine *e)
{
    return e->phase != CAR_IDLE || e->autoStop || e->buttonStop;
}
