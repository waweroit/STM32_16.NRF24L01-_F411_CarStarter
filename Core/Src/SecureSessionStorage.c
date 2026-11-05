#include "SecureSessionStorage.h"
#include "CarStorage.h"
bool SecureSessionStorage_NextBootCounter(uint32_t *bootCounter)
{
    if (!bootCounter || !CarStorage_Ready()) return false;
    CarStoredData data = *CarStorage_Get();
    if (data.bootCounter >= 0xFFFFFFu) return false;
    ++data.bootCounter;
    if (!CarStorage_Save(&data)) return false;
    *bootCounter = data.bootCounter;
    return true;
}
