/* HMAC-SHA256 against RFC 4231 test cases 1, 2 and 6 (long key). */
#include <stdio.h>
#include <string.h>

#include "audioio/hmac_sha256.h"

static int check(const char *name, const unsigned char *key, size_t keylen,
                 const char *msg, const char *expect_hex)
{
	unsigned char mac[SHA256_LEN];
	char hex[2 * SHA256_LEN + 1];
	size_t n = strlen(msg);
	/* split the message in two parts to cover msg1 || msg2 */
	hmac_sha256(key, keylen, (const unsigned char *) msg, n / 2,
		(const unsigned char *) msg + n / 2, n - n / 2, mac);
	for (int i = 0; i < SHA256_LEN; i++)
		sprintf(hex + 2 * i, "%02x", mac[i]);
	if (strcmp(hex, expect_hex) != 0) {
		printf("FAIL %s: %s\n", name, hex);
		return 1;
	}
	printf("ok %s\n", name);
	return 0;
}

int main(void)
{
	unsigned char k1[20], k6[131];
	memset(k1, 0x0b, sizeof(k1));
	memset(k6, 0xaa, sizeof(k6));
	int fail = 0;
	fail |= check("rfc4231-1", k1, sizeof(k1), "Hi There",
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	fail |= check("rfc4231-2", (const unsigned char *) "Jefe", 4,
		"what do ya want for nothing?",
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	fail |= check("rfc4231-6", k6, sizeof(k6),
		"Test Using Larger Than Block-Size Key - Hash Key First",
		"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
	return fail;
}
