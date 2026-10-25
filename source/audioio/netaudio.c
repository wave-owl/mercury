/* Socket audio device ("TCP:<port>") for Mercury.  See audioio/netaudio.h for
 * the wire format.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The capture thread moves 'R' samples into Mercury's capture buffer and
 * records 'D' acknowledgements; the playback thread drains Mercury's playback
 * buffer into paced 'T' messages; netaudio_flush (called by the ARQ
 * controller before "PTT OFF") sends 'E' and waits for the matching 'D'.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "audioio/hmac_sha256.h"
#include "audioio/netaudio.h"

extern bool shutdown_;

#ifdef MSG_NOSIGNAL
#define NA_SEND_FLAGS MSG_NOSIGNAL
#else
#define NA_SEND_FLAGS 0
#endif

#define NA_MAX_PAYLOAD (1 << 20)
#define NA_MAX_TOKEN 128
#define NA_LEAD_MS 500          /* TX audio may run this far ahead of real time */
#define NA_SEND_TIMEOUT_MS 1000
#define NA_AUTH_TIMEOUT_MS 3000
#define NA_FLUSH_EXTRA_MS 2000  /* PTT OFF at the latest length + this */
#define NA_CHUNK 4800           /* 100 ms at 48 kHz per T message */
#define NA_MS_SAMPLES (NETAUDIO_RATE / 1000)

static int na_fd = -1;
static volatile bool na_broken = false;
static volatile bool na_closing = false;  /* orderly shutdown in progress */
static pthread_mutex_t na_lock = PTHREAD_MUTEX_INITIALIZER;      /* D bookkeeping */
static pthread_mutex_t na_send_lock = PTHREAD_MUTEX_INITIALIZER; /* one message at a time */
static pthread_mutex_t na_tx_lock = PTHREAD_MUTEX_INITIALIZER;   /* T stream vs. E */
static uint32_t na_tx_seq = 0;      /* number of the current/last transmission */
static uint32_t na_last_d = 0;      /* number in the last 'D' received */
static bool na_have_d = false;
static uint64_t na_burst_start = 0; /* ms at the first sample of the transmission */
static uint64_t na_burst_samples = 0;

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts = {ms / 1000, (long) (ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}

static void put_u32(unsigned char *b, uint32_t v)
{
	b[0] = v & 0xFF;
	b[1] = (v >> 8) & 0xFF;
	b[2] = (v >> 16) & 0xFF;
	b[3] = (v >> 24) & 0xFF;
}

static uint32_t get_u32(const unsigned char *b)
{
	return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16)
		| ((uint32_t) b[3] << 24);
}

/* Fail closed: a broken socket audio connection ends Mercury at once.  The
 * ARQ and test loops may block on the full playback buffer, so a flag alone
 * would not end the process; the host sees the control port close. */
static void mark_broken(const char *why)
{
	shutdown_ = true;
	if (na_closing)
		return;
	if (!na_broken) {
		na_broken = true;
		fprintf(stderr, "Socket audio: %s; Mercury exits\n", why);
		fflush(stderr);
		fflush(stdout);
		_exit(3);
	}
}

bool netaudio_is_dev(const char *devstr)
{
	return devstr != NULL
		&& strncmp(devstr, NETAUDIO_PREFIX, strlen(NETAUDIO_PREFIX)) == 0;
}

bool netaudio_active(void)
{
	return na_fd >= 0 && !na_broken;
}

static bool send_all(int fd, const unsigned char *p, size_t n)
{
	while (n > 0) {
		ssize_t w = send(fd, p, n, NA_SEND_FLAGS);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;  /* SO_SNDTIMEO bounds a stalled peer */
		p += w;
		n -= (size_t) w;
	}
	return true;
}

static bool send_raw(int fd, char type, const void *payload, uint32_t len)
{
	unsigned char hdr[5];
	hdr[0] = (unsigned char) type;
	put_u32(hdr + 1, len);
	return send_all(fd, hdr, sizeof(hdr))
		&& (len == 0 || send_all(fd, (const unsigned char *) payload, len));
}

/* One message on the open connection; a failure (or a send stalled for
 * NA_SEND_TIMEOUT_MS) breaks the connection. */
static bool send_msg(char type, const void *payload, uint32_t len)
{
	bool ok;
	pthread_mutex_lock(&na_send_lock);
	ok = na_fd >= 0 && !na_broken && send_raw(na_fd, type, payload, len);
	pthread_mutex_unlock(&na_send_lock);
	if (!ok)
		mark_broken(errno == EAGAIN || errno == EWOULDBLOCK
			? "host stopped reading (send stalled)" : "send failed");
	return ok;
}

static bool recv_exact(int fd, void *buf, size_t n)
{
	size_t got = 0;
	while (got < n) {
		ssize_t r = recv(fd, (char *) buf + got, n - got, 0);
		if (r == 0)
			return false;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		got += (size_t) r;
	}
	return true;
}

/* recv exactly n bytes before the absolute deadline (one budget for the whole
 * handshake, so a peer trickling bytes cannot extend it). */
static bool recv_deadline(int fd, void *buf, size_t n, uint64_t deadline)
{
	size_t got = 0;
	while (got < n) {
		uint64_t now = now_ms();
		if (now >= deadline)
			return false;
		struct pollfd p = {fd, POLLIN, 0};
		int r = poll(&p, 1, (int) (deadline - now));
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		ssize_t k = recv(fd, (char *) buf + got, n - got, 0);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			return false;
		got += (size_t) k;
	}
	return true;
}

static bool random_bytes(unsigned char *buf, size_t n)
{
	FILE *f = fopen("/dev/urandom", "rb");
	if (f == NULL)
		return false;
	bool ok = fread(buf, 1, n, f) == n;
	fclose(f);
	return ok;
}

static void to_hex(const unsigned char *b, size_t n, char *out)
{
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		out[2 * i] = digits[b[i] >> 4];
		out[2 * i + 1] = digits[b[i] & 0x0F];
	}
	out[2 * n] = 0x00;
}

static bool equal_ct(const unsigned char *a, const unsigned char *b, size_t n)
{
	unsigned char diff = 0;
	for (size_t i = 0; i < n; i++)
		diff |= (unsigned char) (a[i] ^ b[i]);
	return diff == 0;
}

/* Parse "TCP:<port>" or "TCP:<host>:<port>", resolve, and connect to loopback
 * addresses only. */
static int connect_dev(const char *devstr)
{
	char host[64] = "127.0.0.1";
	char port[16];
	const char *spec = devstr + strlen(NETAUDIO_PREFIX);
	const char *colon = strrchr(spec, ':');
	if (colon != NULL) {
		size_t hlen = (size_t) (colon - spec);
		if (hlen == 0 || hlen >= sizeof(host))
			return -1;
		memcpy(host, spec, hlen);
		host[hlen] = 0;
		snprintf(port, sizeof(port), "%s", colon + 1);
	} else {
		snprintf(port, sizeof(port), "%s", spec);
	}
	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
		fprintf(stderr, "Socket audio: cannot resolve %s\n", devstr);
		return -1;
	}
	bool loopback = false;
	if (res->ai_family == AF_INET) {
		struct sockaddr_in *a = (struct sockaddr_in *) res->ai_addr;
		loopback = (ntohl(a->sin_addr.s_addr) >> 24) == 127;
	} else if (res->ai_family == AF_INET6) {
		struct sockaddr_in6 *a = (struct sockaddr_in6 *) res->ai_addr;
		loopback = IN6_IS_ADDR_LOOPBACK(&a->sin6_addr);
	}
	if (!loopback) {
		fprintf(stderr, "Socket audio: %s is not a loopback address; refused\n", devstr);
		freeaddrinfo(res);
		return -1;
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		fprintf(stderr, "Socket audio: cannot connect to %s\n", devstr);
		return -1;
	}
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	struct timeval tv = {NA_SEND_TIMEOUT_MS / 1000, (NA_SEND_TIMEOUT_MS % 1000) * 1000};
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	/* A small send buffer (about 0.2 s of audio) makes a host that stops
	 * reading visible within a second. */
	int sndbuf = 16384;
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	return fd;
}

/* Mutual challenge-response (protocol 3), all within NA_AUTH_TIMEOUT_MS:
 *   host -> 'N' nonce1 (32 bytes)
 *   mercury -> 'H' "MERCURY-NETAUDIO 3 48000 <hex nonce2> <hex HMAC(token, nonce1||"engine")>"
 *   host -> 'P' HMAC(token, nonce2||"bridge") (32 bytes)
 * The token itself never crosses the connection. */
static bool handshake(int fd, const char *token)
{
	uint64_t deadline = now_ms() + NA_AUTH_TIMEOUT_MS;
	const unsigned char *key = (const unsigned char *) token;
	size_t keylen = strlen(token);
	unsigned char hdr[5], nonce1[32], nonce2[32], mac[SHA256_LEN], got[SHA256_LEN];
	if (!recv_deadline(fd, hdr, sizeof(hdr), deadline) || hdr[0] != 'N'
		|| get_u32(hdr + 1) != sizeof(nonce1)
		|| !recv_deadline(fd, nonce1, sizeof(nonce1), deadline))
		return false;
	if (!random_bytes(nonce2, sizeof(nonce2)))
		return false;
	hmac_sha256(key, keylen, nonce1, sizeof(nonce1), (const unsigned char *) "engine", 6,
		mac);
	char hello[64 + 4 * SHA256_LEN + 4], n2hex[65], machex[65];
	to_hex(nonce2, sizeof(nonce2), n2hex);
	to_hex(mac, sizeof(mac), machex);
	int len = snprintf(hello, sizeof(hello), "%s %d %d %s %s", NETAUDIO_HELLO,
		NETAUDIO_VERSION, NETAUDIO_RATE, n2hex, machex);
	if (!send_raw(fd, 'H', hello, (uint32_t) len))
		return false;
	if (!recv_deadline(fd, hdr, sizeof(hdr), deadline) || hdr[0] != 'P'
		|| get_u32(hdr + 1) != sizeof(got)
		|| !recv_deadline(fd, got, sizeof(got), deadline))
		return false;
	hmac_sha256(key, keylen, nonce2, sizeof(nonce2), (const unsigned char *) "bridge", 6,
		mac);
	return equal_ct(mac, got, sizeof(mac));
}

bool netaudio_open(const char *devstr)
{
	if (na_fd >= 0)
		return true;
	const char *token = getenv(NETAUDIO_TOKEN_ENV);
	if (token == NULL || strlen(token) < 16 || strlen(token) > NA_MAX_TOKEN) {
		fprintf(stderr, "Socket audio refused: %s must be set (16..%d characters) by the"
			" program that starts Mercury\n", NETAUDIO_TOKEN_ENV, NA_MAX_TOKEN);
		return false;
	}
	int fd = connect_dev(devstr);
	if (fd < 0)
		return false;
	if (!handshake(fd, token)) {
		fprintf(stderr, "Socket audio: the host at %s did not authenticate; refused\n",
			devstr);
		close(fd);
		return false;
	}
	na_broken = false;
	na_have_d = false;
	na_burst_samples = 0;
	na_fd = fd;
	printf("Socket audio connected to %s (%d Hz)\n", devstr, NETAUDIO_RATE);
	fflush(stdout);
	return true;
}

void netaudio_close(void)
{
	int fd = na_fd;
	if (fd < 0)
		return;
	na_closing = true;
	shutdown(fd, SHUT_RDWR);  /* wakes the capture thread */
}

void *netaudio_capture_thread(void *capture_cbuf)
{
	cbuf_handle_t capture = (cbuf_handle_t) capture_cbuf;
	unsigned char hdr[5];
	unsigned char *buf = (unsigned char *) malloc(NA_MAX_PAYLOAD);
	double *samples = (double *) malloc((NA_MAX_PAYLOAD / 2) * sizeof(double));
	time_t last_warn = 0;
	unsigned int suppressed = 0;
	while (!shutdown_ && buf != NULL && samples != NULL) {
		if (!recv_exact(na_fd, hdr, sizeof(hdr)))
			break;
		uint32_t len = get_u32(hdr + 1);
		if (len > NA_MAX_PAYLOAD || (hdr[0] == 'R' && (len & 1))) {
			fprintf(stderr, "Socket audio: bad message 0x%02X length %u\n", hdr[0], len);
			break;
		}
		if (len && !recv_exact(na_fd, buf, len))
			break;
		if (hdr[0] == 'R') {
			size_t n = len / 2;
			for (size_t i = 0; i < n; i++) {
				int16_t v = (int16_t) (buf[2 * i] | (buf[2 * i + 1] << 8));
				samples[i] = (double) v / 32768.0;
			}
			if (circular_buf_free_size(capture) >= n * sizeof(double))
				write_buffer(capture, (uint8_t *) samples, n * sizeof(double));
		} else if (hdr[0] == 'D' && len == 4) {
			pthread_mutex_lock(&na_lock);
			na_last_d = get_u32(buf);
			na_have_d = true;
			pthread_mutex_unlock(&na_lock);
		} else {
			/* Rate-limited: a misbehaving host must not flood the log. */
			time_t now = time(NULL);
			if (now - last_warn >= 10) {
				fprintf(stderr, "Socket audio: ignoring message 0x%02X (len %u)"
					" (+%u suppressed)\n", hdr[0], len, suppressed);
				last_warn = now;
				suppressed = 0;
			} else {
				suppressed++;
			}
		}
	}
	free(buf);
	free(samples);
	mark_broken("connection closed by host");
	printf("netaudio_capture_thread exit\n");
	return NULL;
}

void *netaudio_playback_thread(void *playback_cbuf)
{
	cbuf_handle_t playback = (cbuf_handle_t) playback_cbuf;
	double in[NA_CHUNK];
	unsigned char out[2 * NA_CHUNK];
	while (!shutdown_ && !na_broken) {
		size_t avail = size_buffer(playback) / sizeof(double);
		if (avail == 0) {
			sleep_ms(2);
			continue;
		}
		size_t n = avail > NA_CHUNK ? NA_CHUNK : avail;
		pthread_mutex_lock(&na_tx_lock);
		if (na_burst_samples == 0)
			na_burst_start = now_ms();
		/* Pace: never more than NA_LEAD_MS ahead of real time, so the ARQ
		 * timers that wait for the playback buffer to drain follow the radio. */
		while (!shutdown_ && !na_broken
			&& na_burst_samples / NA_MS_SAMPLES > now_ms() - na_burst_start + NA_LEAD_MS) {
			pthread_mutex_unlock(&na_tx_lock);
			sleep_ms(2);
			pthread_mutex_lock(&na_tx_lock);
		}
		read_buffer(playback, (uint8_t *) in, n * sizeof(double));
		for (size_t i = 0; i < n; i++) {
			double x = in[i];
			if (x > 1.0)
				x = 1.0;
			if (x < -1.0)
				x = -1.0;
			int16_t v = (int16_t) (x * 32767.0);
			out[2 * i] = (unsigned char) (v & 0xFF);
			out[2 * i + 1] = (unsigned char) ((v >> 8) & 0xFF);
		}
		bool ok = send_msg('T', out, (uint32_t) (2 * n));
		if (ok)
			na_burst_samples += n;
		pthread_mutex_unlock(&na_tx_lock);
		if (!ok)
			break;
	}
	printf("netaudio_playback_thread exit\n");
	shutdown_ = true;
	return NULL;
}

bool netaudio_flush(void)
{
	pthread_mutex_lock(&na_tx_lock);  /* every 'T' of this transmission is sent */
	uint32_t seq = ++na_tx_seq;
	uint64_t start = now_ms();
	uint64_t first = na_burst_samples ? na_burst_start : start;
	uint64_t length_ms = na_burst_samples / NA_MS_SAMPLES;
	/* 'D' is honoured only once the transmission could have been played. */
	uint64_t min_end = first + length_ms;
	/* Wall clock from the first sample, so pacing does not extend PTT. */
	uint64_t deadline = first + length_ms + NA_FLUSH_EXTRA_MS;
	na_burst_samples = 0;
	unsigned char payload[4];
	put_u32(payload, seq);
	/* Only a D received after this E counts. */
	pthread_mutex_lock(&na_lock);
	na_have_d = false;
	pthread_mutex_unlock(&na_lock);
	bool sent = send_msg('E', payload, sizeof(payload));
	pthread_mutex_unlock(&na_tx_lock);
	if (!sent)
		return false;
	while (!na_broken) {
		uint64_t now = now_ms();
		pthread_mutex_lock(&na_lock);
		bool acked = na_have_d && na_last_d == seq;
		pthread_mutex_unlock(&na_lock);
		if (acked && now >= min_end)
			return true;
		if (now >= deadline)
			break;
		sleep_ms(2);
	}
	mark_broken("no drain acknowledgement for the transmission in time");
	return false;
}
