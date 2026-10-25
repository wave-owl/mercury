/* SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104) for the socket audio handshake.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_LEN 32

void sha256(const unsigned char *data, size_t len, unsigned char out[SHA256_LEN]);

/* HMAC-SHA256(key, msg1 || msg2); msg2 may be NULL with len2 0. */
void hmac_sha256(const unsigned char *key, size_t keylen,
                 const unsigned char *msg1, size_t len1,
                 const unsigned char *msg2, size_t len2,
                 unsigned char out[SHA256_LEN]);

#ifdef __cplusplus
}
#endif
