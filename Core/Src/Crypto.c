#include "Crypto.h"
#include "tiny_aes.h"
#include <stdbool.h>
#include <string.h>

#define CMAC_RB 0x87u

static void left_shift_block(const uint8_t input[16], uint8_t output[16])
{
    uint8_t carry = 0u;
    int i;
    for (i = 15; i >= 0; --i) {
        uint8_t nextCarry = (uint8_t)((input[i] >> 7u) & 1u);
        output[i] = (uint8_t)((input[i] << 1u) | carry);
        carry = nextCarry;
    }
}

static void xor_block(const uint8_t a[16], const uint8_t b[16], uint8_t output[16])
{
    size_t i;
    for (i = 0u; i < 16u; ++i) {
        output[i] = (uint8_t)(a[i] ^ b[i]);
    }
}

static void generate_cmac_subkeys(const uint8_t key[16],
                                  uint8_t subkey1[16],
                                  uint8_t subkey2[16])
{
    struct AES_ctx context;
    uint8_t l[16] = {0};

    AES_init_ctx(&context, key);
    AES_ECB_encrypt(&context, l);

    left_shift_block(l, subkey1);
    if ((l[0] & 0x80u) != 0u) {
        subkey1[15] ^= CMAC_RB;
    }

    left_shift_block(subkey1, subkey2);
    if ((subkey1[0] & 0x80u) != 0u) {
        subkey2[15] ^= CMAC_RB;
    }
}

static CryptoStatus_t ctr_xcrypt(const uint8_t key[16],
                                 const uint8_t nonceCounter[16],
                                 const uint8_t *input,
                                 uint8_t *output,
                                 size_t length)
{
    struct AES_ctx context;

    if (key == NULL || nonceCounter == NULL ||
        (length > 0u && (input == NULL || output == NULL))) {
        return CRYPTO_INVALID_ARGUMENT;
    }
    if (length == 0u) {
        return CRYPTO_OK;
    }

    if (output != input) {
        memmove(output, input, length);
    }
    AES_init_ctx_iv(&context, key, nonceCounter);
    AES_CTR_xcrypt_buffer(&context, output, length);
    return CRYPTO_OK;
}

CryptoStatus_t AES_CTR_Encrypt(const uint8_t key[16],
                              const uint8_t nonceCounter[16],
                              const uint8_t *plainText,
                              uint8_t *cipherText,
                              size_t length)
{
    return ctr_xcrypt(key, nonceCounter, plainText, cipherText, length);
}

CryptoStatus_t AES_CTR_Decrypt(const uint8_t key[16],
                              const uint8_t nonceCounter[16],
                              const uint8_t *cipherText,
                              uint8_t *plainText,
                              size_t length)
{
    return ctr_xcrypt(key, nonceCounter, cipherText, plainText, length);
}

CryptoStatus_t Crypto_CalculateCmac(const uint8_t key[16],
                                   const uint8_t *data,
                                   size_t dataLength,
                                   uint8_t mac[16])
{
    struct AES_ctx context;
    uint8_t subkey1[16];
    uint8_t subkey2[16];
    uint8_t state[16] = {0};
    uint8_t block[16];
    uint8_t finalBlock[16];
    size_t blockCount;
    size_t i;
    size_t j;
    bool finalBlockComplete;

    if (key == NULL || mac == NULL ||
        (dataLength > 0u && data == NULL)) {
        return CRYPTO_INVALID_ARGUMENT;
    }

    generate_cmac_subkeys(key, subkey1, subkey2);
    blockCount = (dataLength + 15u) / 16u;
    if (blockCount == 0u) {
        blockCount = 1u;
    }
    finalBlockComplete = dataLength != 0u && (dataLength % 16u) == 0u;

    if (finalBlockComplete) {
        memcpy(finalBlock, &data[16u * (blockCount - 1u)], 16u);
        xor_block(finalBlock, subkey1, finalBlock);
    } else {
        size_t remainder = dataLength == 0u ? 0u : dataLength % 16u;
        memset(finalBlock, 0, sizeof(finalBlock));
        if (remainder > 0u) {
            memcpy(finalBlock, &data[16u * (blockCount - 1u)], remainder);
        }
        finalBlock[remainder] = 0x80u;
        xor_block(finalBlock, subkey2, finalBlock);
    }

    AES_init_ctx(&context, key);
    for (i = 0u; i < blockCount - 1u; ++i) {
        for (j = 0u; j < 16u; ++j) {
            block[j] = (uint8_t)(state[j] ^ data[i * 16u + j]);
        }
        AES_ECB_encrypt(&context, block);
        memcpy(state, block, sizeof(state));
    }

    for (j = 0u; j < 16u; ++j) {
        block[j] = (uint8_t)(state[j] ^ finalBlock[j]);
    }
    AES_ECB_encrypt(&context, block);
    memcpy(mac, block, 16u);
    return CRYPTO_OK;
}

CryptoStatus_t Crypto_CalculateAuthTag(const uint8_t authenticationKey[16],
                                      const uint8_t *data,
                                      size_t dataLength,
                                      uint8_t tag[8])
{
    uint8_t mac[16];
    CryptoStatus_t status;

    if (tag == NULL) {
        return CRYPTO_INVALID_ARGUMENT;
    }
    status = Crypto_CalculateCmac(authenticationKey, data, dataLength, mac);
    if (status != CRYPTO_OK) {
        return status;
    }
    memcpy(tag, mac, CRYPTO_AUTH_TAG_SIZE);
    return CRYPTO_OK;
}

CryptoStatus_t Crypto_VerifyAuthTag(const uint8_t authenticationKey[16],
                                   const uint8_t *data,
                                   size_t dataLength,
                                   const uint8_t expectedTag[8])
{
    uint8_t actualTag[8];
    uint8_t difference = 0u;
    size_t i;
    CryptoStatus_t status;

    if (expectedTag == NULL) {
        return CRYPTO_INVALID_ARGUMENT;
    }
    status = Crypto_CalculateAuthTag(authenticationKey, data, dataLength,
                                     actualTag);
    if (status != CRYPTO_OK) {
        return status;
    }

    for (i = 0u; i < CRYPTO_AUTH_TAG_SIZE; ++i) {
        difference |= (uint8_t)(actualTag[i] ^ expectedTag[i]);
    }
    return difference == 0u ? CRYPTO_OK : CRYPTO_AUTHENTICATION_FAILED;
}

const char *Crypto_StatusToString(CryptoStatus_t status)
{
    switch (status) {
    case CRYPTO_OK: return "CRYPTO_OK";
    case CRYPTO_INVALID_ARGUMENT: return "CRYPTO_INVALID_ARGUMENT";
    case CRYPTO_AUTHENTICATION_FAILED: return "CRYPTO_AUTHENTICATION_FAILED";
    case CRYPTO_INTERNAL_ERROR: return "CRYPTO_INTERNAL_ERROR";
    default: return "CRYPTO_UNKNOWN";
    }
}
