#ifndef DEVICE_KEYS_H
#define DEVICE_KEYS_H

#include <stdint.h>
#include "Crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Derive a 128-bit directional key with AES-CMAC from the shared communication key.
 * The source and destination IDs are part of the KDF context.
 */
CryptoStatus_t DeviceKeys_DeriveConnectionKey(
    const uint8_t masterKey[CRYPTO_AES_KEY_SIZE],
    const char label[4],
    uint8_t sourceId,
    uint8_t destinationId,
    uint8_t derivedKey[CRYPTO_AES_KEY_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_KEYS_H */
