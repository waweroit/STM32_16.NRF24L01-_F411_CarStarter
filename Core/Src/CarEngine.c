#include "CarEngine.h"
#include "CarConfig.h"
#include <string.h>
/** Zeruje stan sterownika e, kopiuje funkcje sterowania wyjściami i zgłaszania wyniku z io
 * oraz ustawia czas do zgaszenia (seconds, w sekundach). */
void CarEngine_Init(CarEngine *e, const CarEngineIo *io, uint32_t seconds)
{
    memset(e, 0, sizeof(*e)); e->io = *io; e->timeoutSeconds = seconds;
    CarEngine_SetThresholds(e, CAR_DEFAULT_RUNNING_MV, CAR_DEFAULT_STOPPED_MV);
    e->io.key(false); e->io.button(false);
}
/** Rozpoczyna sekwencję rozruchu sterownika e i zwraca kod przyjęcia lub odmowy. now to czas
 * HAL w ms, mv to pomiar dla opcjonalnego minimum przed startem, valid oznacza ważny pomiar, a request
 * identyfikuje żądanie w późniejszym wyniku. */
CarResult CarEngine_Start(CarEngine *e, uint32_t now, uint16_t mv, bool valid, uint32_t request)
{
    if (e->phase != CAR_IDLE || e->buttonStop || e->autoStop) return CAR_RESULT_BUSY;
    if (!valid) return CAR_RESULT_SENSOR_ERROR;
    if (e->status != CAR_ENGINE_STOPPED) return CAR_RESULT_ALREADY_RUNNING;
#if CAR_MIN_START_MV > 0
    if (mv < CAR_MIN_START_MV) return CAR_RESULT_SENSOR_ERROR;
#else
    (void)mv;
#endif
    e->request = request; e->takeover = false;
    e->phase = CAR_KEY_WAIT; e->phaseAt = now; e->io.key(true);
    return CAR_RESULT_ACCEPTED;
}
/** Zleca impuls gaszenia sterownikowi e. now wyznacza początek impulsu w ms, a valid określa
 * ważność pomiaru napięcia. Wynik informuje, czy impuls był potrzebny i czy go rozpoczęto. */
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
/** Aktualizuje fazę i liczniki sterownika e na podstawie czasu HAL (now, ms) i pomiaru
 * napięcia (mv, mV). Flaga valid określa, czy można użyć pomiaru do oceny pracy silnika. */
void CarEngine_Process(CarEngine *e, uint32_t now, uint16_t mv, bool valid)
{
    uint32_t elapsed = now - e->phaseAt;
    if (e->phase == CAR_KEY_WAIT && elapsed >= CAR_KEY_SETTLE_MS) {
        e->io.button(true); e->phase = CAR_START_PULSE; e->phaseAt = now;
    } else if (e->phase == CAR_START_PULSE && elapsed >= CAR_BUTTON_PULSE_MS) {
        e->io.button(false); e->phase = CAR_START_OBSERVE; e->phaseAt = now;
    } else if (e->phase == CAR_START_OBSERVE) {
        bool running = valid && mv >= e->runningMv;
        if (!running && elapsed < CAR_START_OBSERVE_MS) return;
        e->io.key(false); e->phase = CAR_IDLE;
        e->status = running ? CAR_ENGINE_RUNNING : CAR_ENGINE_UNKNOWN;
        e->autoStop = running && !e->takeover; e->startedAt = now;
        e->io.result(running ? CAR_RESULT_STARTED : CAR_RESULT_START_FAILED, e->request);
    } else if (e->phase == CAR_STOP_PULSE && elapsed >= CAR_BUTTON_PULSE_MS) {
        e->io.button(false); e->phase = CAR_STOP_VERIFY; e->phaseAt = now;
    } else if (e->phase == CAR_STOP_VERIFY && elapsed >= CAR_STOP_SETTLE_MS &&
               ((valid && mv < e->stoppedMv) || elapsed >= CAR_STOP_VERIFY_MS)) {
        e->phase = CAR_IDLE;
        bool stopped = valid && mv < e->stoppedMv;
        e->status = stopped ? CAR_ENGINE_STOPPED : CAR_ENGINE_UNKNOWN;
        e->io.result(stopped ? CAR_RESULT_STOPPED : CAR_RESULT_STOP_FAILED, e->request);
    }
    if (e->phase != CAR_IDLE) return;
    /* Między progami pracy i postoju zachowujemy poprzedni stan, aby wahania napięcia
     * nie przełączały wskazania. */
    if (!valid) e->status = CAR_ENGINE_UNKNOWN;
    else if (mv >= e->runningMv) e->status = CAR_ENGINE_RUNNING;
    else if (mv < e->stoppedMv) e->status = CAR_ENGINE_STOPPED;
    if (e->status != CAR_ENGINE_UNKNOWN) e->lastKnownRunning = e->status == CAR_ENGINE_RUNNING;
    /* An externally stopped engine must never receive a delayed toggle pulse. */
    if (e->status == CAR_ENGINE_STOPPED) e->autoStop = e->buttonStop = false;
    bool expired = (e->autoStop && now - e->startedAt >= e->timeoutSeconds * 1000u) ||
                   (e->buttonStop && now - e->buttonStopAt >= CAR_DOUBLE_STOP_MS);
    if (expired) {
        CarResult r = CarEngine_Stop(e, now, valid);
        if (r != CAR_RESULT_ACCEPTED) e->io.result(r, e->request);
    }
}
/** Anuluje automatyczne gaszenie po rozruchu i zapisuje w sterowniku e przejęcie sterowania
 * przez użytkownika. */
void CarEngine_SingleClick(CarEngine *e)
{
    e->autoStop = false; e->takeover = true;
    e->io.result(CAR_RESULT_TIMER_CANCELLED, e->request);
}
/** Uruchamia licznik opóźnionego gaszenia w sterowniku e, gdy silnik pracuje i nie trwa żadna
 * sekwencja. now to czas drugiego kliknięcia w ms. */
void CarEngine_DoubleClick(CarEngine *e, uint32_t now)
{
    if (e->phase != CAR_IDLE || e->status != CAR_ENGINE_RUNNING) return;
    e->buttonStop = true; e->buttonStopAt = now;
}
/** Ustawia w sterowniku e czas pracy po zdalnym rozruchu. seconds podaje ten czas w
 * sekundach. */
void CarEngine_SetTimeout(CarEngine *e, uint32_t seconds) { e->timeoutSeconds = seconds; }
void CarEngine_SetThresholds(CarEngine *e, uint16_t runningMv, uint16_t stoppedMv)
{
    e->runningMv = runningMv;
    e->stoppedMv = stoppedMv;
    e->status = CAR_ENGINE_UNKNOWN;
}
/** Zwraca liczbę sekund do najbliższego gaszenia w sterowniku e, zaokrągloną w górę względem
 * czasu now (ms). Zero oznacza brak licznika lub upływ terminu. */
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
/** Podtrzymuje aktywność po wykryciu pracy silnika, także podczas błędu ADC. */
bool CarEngine_NeedsAwake(const CarEngine *e)
{
    return e->phase != CAR_IDLE || e->autoStop || e->buttonStop || e->lastKnownRunning;
}
