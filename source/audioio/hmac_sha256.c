/* SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104) for the socket audio handshake.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>

#include "audioio/hmac_sha256.h"

typedef struct {
	uint32_t h[8];
	unsigned char block[64];
	size_t used;       /* bytes in block */
	uint64_t total;    /* bytes hashed */
} sha256_ctx;

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
	0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
	0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
	0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
	0xc67178f2};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(sha256_ctx *c, const unsigned char *p)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = ((uint32_t) p[4 * i] << 24) | ((uint32_t) p[4 * i + 1] << 16)
			| ((uint32_t) p[4 * i + 2] << 8) | (uint32_t) p[4 * i + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
	uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = h + S1 + ch + K[i] + w[i];
		uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
		uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
		uint32_t t2 = S0 + maj;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = cc;
		cc = b;
		b = a;
		a = t1 + t2;
	}
	c->h[0] += a;
	c->h[1] += b;
	c->h[2] += cc;
	c->h[3] += d;
	c->h[4] += e;
	c->h[5] += f;
	c->h[6] += g;
	c->h[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(c->h, iv, sizeof(iv));
	c->used = 0;
	c->total = 0;
}

static void sha256_update(sha256_ctx *c, const unsigned char *data, size_t len)
{
	c->total += len;
	while (len > 0) {
		size_t n = 64 - c->used;
		if (n > len)
			n = len;
		memcpy(c->block + c->used, data, n);
		c->used += n;
		data += n;
		len -= n;
		if (c->used == 64) {
			compress(c, c->block);
			c->used = 0;
		}
	}
}

static void sha256_final(sha256_ctx *c, unsigned char out[SHA256_LEN])
{
	uint64_t bits = c->total * 8;
	unsigned char pad = 0x80;
	sha256_update(c, &pad, 1);
	pad = 0;
	while (c->used != 56)
		sha256_update(c, &pad, 1);
	unsigned char len[8];
	for (int i = 0; i < 8; i++)
		len[i] = (unsigned char) (bits >> (56 - 8 * i));
	sha256_update(c, len, 8);
	for (int i = 0; i < 8; i++) {
		out[4 * i] = (unsigned char) (c->h[i] >> 24);
		out[4 * i + 1] = (unsigned char) (c->h[i] >> 16);
		out[4 * i + 2] = (unsigned char) (c->h[i] >> 8);
		out[4 * i + 3] = (unsigned char) c->h[i];
	}
}

void sha256(const unsigned char *data, size_t len, unsigned char out[SHA256_LEN])
{
	sha256_ctx c;
	sha256_init(&c);
	sha256_update(&c, data, len);
	sha256_final(&c, out);
}

void hmac_sha256(const unsigned char *key, size_t keylen,
                 const unsigned char *msg1, size_t len1,
                 const unsigned char *msg2, size_t len2,
                 unsigned char out[SHA256_LEN])
{
	unsigned char k[64], pad[64], inner[SHA256_LEN];
	memset(k, 0, sizeof(k));
	if (keylen > 64)
		sha256(key, keylen, k);
	else
		memcpy(k, key, keylen);
	sha256_ctx c;
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x36;
	sha256_init(&c);
	sha256_update(&c, pad, 64);
	if (len1)
		sha256_update(&c, msg1, len1);
	if (len2)
		sha256_update(&c, msg2, len2);
	sha256_final(&c, inner);
	for (int i = 0; i < 64; i++)
		pad[i] = k[i] ^ 0x5c;
	sha256_init(&c);
	sha256_update(&c, pad, 64);
	sha256_update(&c, inner, SHA256_LEN);
	sha256_final(&c, out);
}
