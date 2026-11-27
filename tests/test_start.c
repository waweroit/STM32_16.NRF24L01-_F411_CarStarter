/* Exercise the production request handler with a simulated clock, ADC and outputs. */
#include <stdint.h>
#define HAL_GetTick StartTest_Tick
#define CarSensors_VoltageValid StartTest_VoltageValid
#define CarSensors_Millivolts StartTest_Millivolts
uint32_t StartTest_Tick(void);
#include "../Core/Src/CarStarter.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint32_t tick;
static bool keyOn, pulseOn, adcValid;
static uint16_t voltage;
uint32_t StartTest_Tick(void) { return tick; }
bool StartTest_VoltageValid(uint32_t now) { (void)now; return adcValid; }
uint16_t StartTest_Millivolts(void) { return voltage; }
bool UsbDebug_IsVerbose(void) { return false; }
void DebugPrintf(const char *format, ...) { (void)format; }
static void TestKey(bool on) { keyOn=on; }
static void TestButton(bool on) { pulseOn=on; }

int StartRequestTests(W25Q_HandleTypeDef *device)
{
    CHECK(CarStorage_Init(device));
    CarEngineIo io={TestKey,TestButton,EngineResult};
    CarEngine_Init(&engine,&io,CarStorage_Get()->stopSeconds);
    engine.status=CAR_ENGINE_STOPPED;
    storageOk=true;
    secure.localSessionId=2u;
    window=1u; cycleAt=0u; tick=100u;
    adcValid=true; voltage=12200u;
    CommunicationMessage_t message={.payloadLength=11u};
    Car_Put32(message.payload,secure.localSessionId);
    Car_Put16(message.payload+4,window);
    Car_Put32(message.payload+6,42u);
    uint32_t request=0u;

    message.payload[10]=31u;
    CHECK(StartFromMessage(&message,tick,&request)==CAR_RESULT_BAD_REQUEST);
    message.payload[10]=10u;
    adcValid=false;
    CHECK(StartFromMessage(&message,tick,&request)==CAR_RESULT_SENSOR_ERROR);
    CHECK(!keyOn && !pulseOn && !CarStorage_Get()->lastStartRequest);

    adcValid=true;
    CHECK(StartFromMessage(&message,tick,&request)==CAR_RESULT_ACCEPTED && request==42u);
    CHECK(keyOn && !pulseOn && engine.timeoutSeconds==600u);
    CHECK(CarStorage_Init(device));
    CHECK(CarStorage_Get()->lastStartRequest==42u && CarStorage_Get()->stopSeconds==600u);
    CarEngine_Process(&engine,300u,12200u,true); CHECK(keyOn && pulseOn);
    CarEngine_Process(&engine,2499u,14200u,true); CHECK(keyOn && pulseOn);
    CarEngine_Process(&engine,2500u,14200u,true); CHECK(keyOn && !pulseOn);
    CHECK(keyOn && !pulseOn && resultCount==0u);
    CarEngine_Process(&engine,3500u,14200u,true);
    CHECK(!keyOn && engine.autoStop && CarEngine_Remaining(&engine,3500u)==600u);
    CHECK(resultCount==1u && results[resultHead].code==CAR_RESULT_STARTED && results[resultHead].request==42u);
    CHECK(reporting && reportStep==REPORT_TELEMETRY);

    CarEngine_Init(&engine,&io,CarStorage_Get()->stopSeconds);
    engine.status=CAR_ENGINE_STOPPED;
    message.payload[10]=20u;
    CHECK(StartFromMessage(&message,tick,&request)==CAR_RESULT_DUPLICATE);
    CHECK(!keyOn && !pulseOn && CarStorage_Get()->stopSeconds==600u);

    /* The clock may cross the end of the token while flash is being written. */
    Car_Put32(message.payload+6,43u);
    tick=CAR_LISTEN_MS;
    CHECK(StartFromMessage(&message,tick-1u,&request)==CAR_RESULT_STALE_WINDOW);
    CHECK(!keyOn && !pulseOn && engine.timeoutSeconds==1200u);

    /* Legacy START continues to use the persisted time. */
    tick=600u;
    message.payloadLength=10u;
    Car_Put32(message.payload+6,44u);
    CHECK(StartFromMessage(&message,tick,&request)==CAR_RESULT_ACCEPTED);
    CHECK(keyOn && !pulseOn && engine.timeoutSeconds==1200u);
    return 0;
}
