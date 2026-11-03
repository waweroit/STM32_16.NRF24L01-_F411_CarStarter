#ifndef COMMUNICATION_DEVICES_H
#define COMMUNICATION_DEVICES_H

#include <stdint.h>
#include "Crypto.h"
#include "NrfLink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMMUNICATION_DEVICE_COUNT 10u

typedef struct
{
    uint8_t deviceId;
    uint8_t deviceKey[CRYPTO_AES_KEY_SIZE];
    uint8_t nrfAddress[NRF_LINK_ADDRESS_SIZE];
} CommunicationDevice_t;

extern const CommunicationDevice_t communicationDevices[COMMUNICATION_DEVICE_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_DEVICES_H */
