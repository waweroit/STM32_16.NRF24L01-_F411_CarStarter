#include "CarEngine.h"
#include "CarConfig.h"
#include "CarStorage.h"
#include "SecureProtocol.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static bool key, pulse;
static unsigned pulseCount, eventCount;
static CarResult lastResult;
static void Key(bool on) { key=on; }
static void Button(bool on) { if (on && !pulse) ++pulseCount; pulse=on; }
static void Event(CarResult r, uint32_t request) { (void)request; lastResult=r; ++eventCount; }
static CarEngineIo io={Key,Button,Event};
static uint8_t nor[8192], backup[8192];
static int failAfter=-1;
static bool readFail;
bool W25Q_Read(W25Q_HandleTypeDef *d, uint32_t addr, uint8_t *data, uint32_t len)
{
    (void)d;
    if (readFail || addr+len>sizeof(nor)) return false;
    memcpy(data,nor+addr,len); return true;
}
bool W25Q_Write(W25Q_HandleTypeDef *d, uint32_t addr, const uint8_t *data, uint32_t len)
{
    (void)d;
    if (addr+len>sizeof(nor)) return false;
    for (uint32_t i=0;i<len;++i) {
        if (failAfter==0) return false;
        if (failAfter>0) --failAfter;
        if ((nor[addr+i]&data[i])!=data[i]) return false;
        nor[addr+i]&=data[i];
    }
    return true;
}
bool W25Q_EraseSector(W25Q_HandleTypeDef *d, uint32_t addr)
{
    (void)d;
    if (addr%4096u || addr+4096u>sizeof(nor)) return false;
    memset(nor+addr,255,4096u); return true;
}
static int EngineTests(void)
{
    CarEngine e;
    CarEngine_Init(&e,&io,900u);
    CHECK(!key && !pulse && e.status==CAR_ENGINE_UNKNOWN);
    CHECK(CarEngine_Start(&e,0u,12000u,false,1u)==CAR_RESULT_SENSOR_ERROR);
    CarEngine_Process(&e,0u,12000u,true);
    CHECK(e.status==CAR_ENGINE_STOPPED);
    CHECK(CarEngine_Start(&e,0u,12000u,true,1u)==CAR_RESULT_ACCEPTED);
    CHECK(key && !pulse);
    CHECK(CarEngine_Start(&e,1u,12000u,true,2u)==CAR_RESULT_BUSY);
    CarEngine_Process(&e,199u,12000u,true); CHECK(!pulse);
    CarEngine_Process(&e,200u,12000u,true); CHECK(pulse);
    CarEngine_Process(&e,2399u,13000u,true); CHECK(pulse);
    CarEngine_Process(&e,2400u,13000u,true); CHECK(!pulse && key);
    CarEngine_Process(&e,3400u,14000u,true);
    CHECK(!key && e.autoStop && lastResult==CAR_RESULT_STARTED);
    unsigned count=pulseCount;
    CarEngine_Process(&e,903399u,14000u,true); CHECK(pulseCount==count);
    CarEngine_Process(&e,903400u,14000u,true); CHECK(pulse && pulseCount==count+1u);
    CarEngine_Process(&e,905600u,12000u,true); CHECK(!pulse);
    CarEngine_Process(&e,910600u,12000u,true); CHECK(lastResult==CAR_RESULT_STOPPED && !CarEngine_NeedsAwake(&e));
    /* Failed start must always release the key and never arm auto-stop. */
    CHECK(CarEngine_Start(&e,920000u,12000u,true,2u)==CAR_RESULT_ACCEPTED);
    CarEngine_Process(&e,920200u,12000u,true);
    CarEngine_Process(&e,922400u,12000u,true);
    CarEngine_Process(&e,937400u,12000u,true);
    CHECK(!key && !pulse && !e.autoStop && lastResult==CAR_RESULT_START_FAILED);
    /* A timer must not toggle a car that was already stopped manually. */
    e.status=CAR_ENGINE_RUNNING; e.autoStop=true; e.startedAt=0u;
    count=pulseCount; CarEngine_Process(&e,999999u,12000u,true);
    CHECK(!e.autoStop && pulseCount==count);
    /* Single click cancels auto-stop, including when pressed during start. */
    CarEngine_Start(&e,1000000u,12000u,true,3u);
    CarEngine_SingleClick(&e);
    CarEngine_Process(&e,1000200u,12000u,true);
    CarEngine_Process(&e,1002400u,12000u,true);
    CarEngine_Process(&e,1003400u,14000u,true);
    CHECK(e.status==CAR_ENGINE_RUNNING && !e.autoStop && !key);
    CarEngine_DoubleClick(&e,1004000u); CHECK(e.buttonStop);
    CarEngine_SingleClick(&e); CHECK(e.buttonStop); /* same as old firmware */
    CarEngine_Process(&e,1024000u,14000u,true); CHECK(pulse);
    /* Tick rollover and changing an active timeout use unsigned elapsed time. */
    CarEngine_Init(&e,&io,900u); e.status=CAR_ENGINE_RUNNING;
    e.autoStop=true; e.startedAt=UINT32_MAX-1000u;
    CHECK(CarEngine_Remaining(&e,999u)==898u);
    CarEngine_SetTimeout(&e,30u);
    CarEngine_Process(&e,29999u,14000u,true); CHECK(pulse);
    /* Missing voltage at expiry must not produce a toggle. */
    CarEngine_Init(&e,&io,30u); e.status=CAR_ENGINE_RUNNING;
    e.autoStop=true; count=pulseCount;
    CarEngine_Process(&e,30000u,14000u,false);
    CHECK(pulseCount==count && !e.autoStop && lastResult==CAR_RESULT_SENSOR_ERROR);
    /* Running car rejects another start. */
    CarEngine_Process(&e,31000u,14000u,true);
    CHECK(CarEngine_Start(&e,31000u,14000u,true,4u)==CAR_RESULT_ALREADY_RUNNING);
    /* A small voltage rise may be inconclusive, but must retain the timer. */
    CarEngine_Init(&e,&io,900u); e.status=CAR_ENGINE_STOPPED;
    CHECK(CarEngine_Start(&e,0u,12600u,true,5u)==CAR_RESULT_ACCEPTED);
    CarEngine_Process(&e,200u,12600u,true);
    CarEngine_Process(&e,2400u,12900u,true);
    CarEngine_Process(&e,17400u,12900u,true);
    CHECK(lastResult==CAR_RESULT_START_FAILED && e.autoStop && !key && !pulse);
    return 0;
}
static int StorageTests(void)
{
    W25Q_HandleTypeDef d={0};
    memset(nor,255,sizeof(nor));
    CHECK(CarStorage_Init(&d)); CHECK(CarStorage_Get()->stopSeconds==900u);
    CarStoredData data=*CarStorage_Get(); data.bootCounter=1u;
    CHECK(CarStorage_Save(&data));
    /* Several rotations, reset after every append; counters/settings survive. */
    for (unsigned i=0;i<180u;++i) {
        data=*CarStorage_Get(); ++data.rootCounter; data.rootInitialized=1u; data.rootSession=0x1234u;
        data.stopSeconds=30u+i;
        CHECK(CarStorage_Save(&data)); CHECK(CarStorage_Init(&d));
        CHECK(CarStorage_Get()->rootCounter==i+1u && CarStorage_Get()->stopSeconds==30u+i);
    }
    memcpy(backup,nor,sizeof(nor));
    /* Power cuts at every byte of a record, including partially written commit. */
    for (int cut=0;cut<64;++cut) {
        memcpy(nor,backup,sizeof(nor)); CHECK(CarStorage_Init(&d));
        data=*CarStorage_Get(); data.stopSeconds=999u; failAfter=cut;
        CHECK(!CarStorage_Save(&data)); failAfter=-1;
        CHECK(CarStorage_Init(&d)); CHECK(CarStorage_Get()->stopSeconds==209u);
        CHECK(CarStorage_Save(&data)); CHECK(CarStorage_Init(&d));
        CHECK(CarStorage_Get()->stopSeconds==999u);
    }
    /* Same power-cut sweep at rollover: old sector must remain recoverable. */
    memset(nor,255,sizeof(nor)); CHECK(CarStorage_Init(&d));
    data=*CarStorage_Get();
    for (unsigned i=0;i<64u;++i) { ++data.bootCounter; CHECK(CarStorage_Save(&data)); }
    memcpy(backup,nor,sizeof(nor));
    for (int cut=0;cut<64;++cut) {
        memcpy(nor,backup,sizeof(nor)); CHECK(CarStorage_Init(&d));
        data=*CarStorage_Get(); data.bootCounter=65u; failAfter=cut;
        CHECK(!CarStorage_Save(&data)); failAfter=-1;
        CHECK(CarStorage_Init(&d)); CHECK(CarStorage_Get()->bootCounter==64u);
    }
    readFail=true; CHECK(!CarStorage_Init(&d)); readFail=false;
    memset(nor,0,sizeof(nor)); CHECK(!CarStorage_Init(&d)); /* no counter reset */
    return 0;
}
static int ProtocolTests(void)
{
    uint8_t keyBytes[16]={1u,2u,3u};
    SecureProtocolContext_t root,car;
    CHECK(SecureProtocol_Init(&root,1u,0x101u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_Init(&car,3u,0x202u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_AddPeer(&car,1u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_AddPeer(&root,3u,keyBytes)==SECURE_PROTOCOL_OK);
    uint8_t frame[32],tampered[32],length=0u,out[11],outLen=0u,source=0u;
    SecureMessageType_t type;
    const uint8_t start[10]={0,0,2,2,0,1,0,0,0,1};
    CHECK(SecureProtocol_CreateFrame(&root,3u,MESSAGE_TYPE_CAR_START,start,10u,frame,32u,&length)==SECURE_PROTOCOL_OK);
    memcpy(tampered,frame,length); tampered[13]^=1u;
    CHECK(SecureProtocol_ProcessFrame(&car,tampered,length,&type,out,sizeof(out),&outLen,&source)==SECURE_PROTOCOL_AUTHENTICATION_FAILED);
    CHECK(SecureProtocol_ProcessFrame(&car,frame,length,&type,out,sizeof(out),&outLen,&source)==SECURE_PROTOCOL_OK);
    CHECK(source==1u && type==MESSAGE_TYPE_CAR_START && outLen==10u && !memcmp(out,start,10u));
    CHECK(SecureProtocol_ProcessFrame(&car,frame,length,&type,out,sizeof(out),&outLen,&source)==SECURE_PROTOCOL_REPLAY_DETECTED);
    uint32_t session=car.peers[0].acceptedRxSessionId,counter=car.peers[0].lastAcceptedRxCounter;
    CHECK(SecureProtocol_Init(&car,3u,0x302u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_AddPeer(&car,1u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_SeedPeerRxState(&car,1u,session,counter,true)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_ProcessFrame(&car,frame,length,&type,out,sizeof(out),&outLen,&source)==SECURE_PROTOCOL_REPLAY_DETECTED);
    CHECK(SecureProtocol_Init(&root,1u,0x201u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_AddPeer(&root,3u,keyBytes)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_CreateFrame(&root,3u,MESSAGE_TYPE_CAR_START,start,10u,frame,32u,&length)==SECURE_PROTOCOL_OK);
    CHECK(SecureProtocol_ProcessFrame(&car,frame,length,&type,out,sizeof(out),&outLen,&source)==SECURE_PROTOCOL_OK);
    return 0;
}
volatile uint32_t test_stage;
int test_main(void)
{
    int r;
    test_stage=1u; r=EngineTests(); if(r) return r;
    test_stage=2u; r=StorageTests(); if(r) return r;
    test_stage=3u; r=ProtocolTests(); if(r) return r;
    test_stage=4u;
    return 0;
}
