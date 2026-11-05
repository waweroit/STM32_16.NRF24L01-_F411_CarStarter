#ifndef CAR_STORAGE_H
#define CAR_STORAGE_H
#include "W25Q64.h"
typedef struct {
    uint32_t bootCounter;
    uint32_t stopSeconds;
    uint32_t rootSession;
    uint32_t rootCounter;
    uint32_t lastStartRequest;
    uint32_t rootInitialized;
} CarStoredData;
bool CarStorage_Init(W25Q_HandleTypeDef *flash);
const CarStoredData *CarStorage_Get(void);
bool CarStorage_Save(const CarStoredData *data);
bool CarStorage_Ready(void);
#endif
