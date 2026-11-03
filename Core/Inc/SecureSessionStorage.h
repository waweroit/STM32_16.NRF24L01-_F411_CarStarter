#ifndef SECURE_SESSION_STORAGE_H
#define SECURE_SESSION_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a new monotonically increasing 24-bit boot counter.
 * The counter is persisted in the reserved last Flash sector.
 */
bool SecureSessionStorage_NextBootCounter(uint32_t *bootCounter);

#ifdef __cplusplus
}
#endif

#endif /* SECURE_SESSION_STORAGE_H */
