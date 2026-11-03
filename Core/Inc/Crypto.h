#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_AES_KEY_SIZE 16u
#define CRYPTO_AES_BLOCK_SIZE 16u
#define CRYPTO_AUTH_TAG_SIZE 8u

typedef enum {
    CRYPTO_OK = 0,
    CRYPTO_INVALID_ARGUMENT,
    CRYPTO_AUTHENTICATION_FAILED,
    CRYPTO_INTERNAL_ERROR
} CryptoStatus_t;

/** Encrypt data using AES-128-CTR. Input and output may be the same buffer. */
CryptoStatus_t AES_CTR_Encrypt(const uint8_t key[CRYPTO_AES_KEY_SIZE],
                              const uint8_t nonceCounter[CRYPTO_AES_BLOCK_SIZE],
                              const uint8_t *plainText, uint8_t *cipherText, size_t length);
/** Decrypt data using AES-128-CTR. */
CryptoStatus_t AES_CTR_Decrypt(const uint8_t key[CRYPTO_AES_KEY_SIZE],
                              const uint8_t nonceCounter[CRYPTO_AES_BLOCK_SIZE],
                              const uint8_t *cipherText, uint8_t *plainText, size_t length);
/** Calculate a complete 16-byte AES-CMAC value (RFC 4493). */
CryptoStatus_t Crypto_CalculateCmac(const uint8_t key[CRYPTO_AES_KEY_SIZE],
                                   const uint8_t *data, size_t dataLength,
                                   uint8_t mac[CRYPTO_AES_BLOCK_SIZE]);
/** Calculate an 8-byte truncated AES-CMAC authentication tag. */
CryptoStatus_t Crypto_CalculateAuthTag(const uint8_t authenticationKey[CRYPTO_AES_KEY_SIZE],
                                      const uint8_t *data, size_t dataLength,
                                      uint8_t tag[CRYPTO_AUTH_TAG_SIZE]);
/** Verify an 8-byte tag using constant-time comparison. */
CryptoStatus_t Crypto_VerifyAuthTag(const uint8_t authenticationKey[CRYPTO_AES_KEY_SIZE],
                                   const uint8_t *data, size_t dataLength,
                                   const uint8_t expectedTag[CRYPTO_AUTH_TAG_SIZE]);
const char *Crypto_StatusToString(CryptoStatus_t status);

#ifdef __cplusplus
}
#endif
#endif
