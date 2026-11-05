/*
 * tiny-AES-c - small portable AES implementation by kokke and contributors.
 * Upstream: https://github.com/kokke/tiny-AES-c
 * Version: v1.0.0 (integration-compatible API)
 * License: The Unlicense / public domain. See THIRD_PARTY_NOTICES.md.
 *
 * Integration changes: header renamed from aes.h to tiny_aes.h; AES-128,
 * ECB block encryption and CTR are enabled. CBC is disabled.
 */
#ifndef TINY_AES_H
#define TINY_AES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CBC 0
#define ECB 1
#define CTR 1
#define AES128 1
#define AES_BLOCKLEN 16u
#define AES_KEYLEN 16u
#define AES_keyExpSize 176u

struct AES_ctx {
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx(struct AES_ctx *ctx, const uint8_t *key);
void AES_init_ctx_iv(struct AES_ctx *ctx, const uint8_t *key, const uint8_t *iv);
void AES_ctx_set_iv(struct AES_ctx *ctx, const uint8_t *iv);
void AES_ECB_encrypt(const struct AES_ctx *ctx, uint8_t *buf);
void AES_CTR_xcrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, size_t length);

#ifdef __cplusplus
}
#endif
#endif
