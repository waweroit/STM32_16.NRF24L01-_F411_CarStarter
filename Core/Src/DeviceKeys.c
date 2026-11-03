#include "DeviceKeys.h"

#define KDF_CONTEXT_VERSION 1u

CryptoStatus_t DeviceKeys_DeriveConnectionKey(const uint8_t masterKey[16],
                                               const char label[4],
                                               uint8_t sourceId,
                                               uint8_t destinationId,
                                               uint8_t derivedKey[16])
{
    uint8_t kdfInput[16] = {0};

    if (masterKey == NULL || label == NULL || derivedKey == NULL)
    {
        return CRYPTO_INVALID_ARGUMENT;
    }

    kdfInput[0] = (uint8_t)label[0];
    kdfInput[1] = (uint8_t)label[1];
    kdfInput[2] = (uint8_t)label[2];
    kdfInput[3] = (uint8_t)label[3];
    kdfInput[4] = sourceId;
    kdfInput[5] = destinationId;
    kdfInput[6] = KDF_CONTEXT_VERSION;
    kdfInput[7] = 0x01u;
    kdfInput[15] = 0x80u;

    return Crypto_CalculateCmac(masterKey,
                                kdfInput,
                                sizeof(kdfInput),
                                derivedKey);
}
