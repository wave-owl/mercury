/* Socket audio device ("TCP:<port>") for Mercury.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lets a host program that owns the radio's sample stream (an SDR such as the
 * Hermes Lite 2, which has no sound card) exchange Mercury's modem audio over
 * one loopback TCP connection instead of a physical or virtual sound card.
 * Same wire format as the socket audio device of the wave-owl ardopcf fork
 * (protocol 3), at Mercury's 48000 Hz.
 *
 * Select it with "-i TCP:<port> -o TCP:<port>" (or "TCP:<host>:<port>" where
 * <host> must be a loopback address; "-x socket" is optional).  Capture and
 * playback must name the same device.  Mercury connects to the host program,
 * which must already listen.
 *
 * Wire format (protocol 3): every message is
 *     type (1 byte) | payload length (uint32, little endian) | payload
 * Handshake (one 3 s budget in total, the token never crosses the wire):
 *     'N' host -> mercury   nonce1 (32 random bytes)
 *     'H' mercury -> host   "MERCURY-NETAUDIO 3 48000 <hex nonce2>
 *                            <hex HMAC-SHA256(token, nonce1 || "engine")>"
 *     'P' host -> mercury   HMAC-SHA256(token, nonce2 || "bridge") (32 bytes)
 * Audio:
 *     'R' host -> mercury   RX audio, int16 little endian mono at 48000 Hz,
 *                           sent in real time by the host (it owns the clock)
 *     'T' mercury -> host   TX audio, int16 little endian mono at 48000 Hz,
 *                           paced to at most 500 ms ahead of real time; sent
 *                           only while Mercury has audio to play (no idle
 *                           silence)
 *     'E' mercury -> host   end of transmission number N (uint32 LE payload),
 *                           sent before "PTT OFF"
 *     'D' host -> mercury   transmission N has left the radio (uint32 LE);
 *                           only a D for the current N received after its E
 *                           counts
 *
 * The token comes from the environment variable MERCURY_NETAUDIO_TOKEN
 * (16..128 characters), set by the program that starts Mercury; without it the
 * device is refused.  Both sides prove knowledge of the token.
 *
 * PTT: Mercury reports "PTT ON" / "PTT OFF" on its control port and the host
 * keys the radio.  With socket audio Mercury sends "PTT OFF" only after 'D'
 * for the current transmission AND at least the transmission's duration since
 * its first sample, or after the transmission's length + 2 s (wall clock);
 * then it exits with status 3 (fail closed).  A send that stalls for 1 s or a
 * lost connection ends Mercury the same way.  The audio ring buffers are private to the
 * process (no shared memory) in this mode.
 *
 * Only built for Unix-like platforms (Linux, macOS).
 */

#pragma once

#include <stdbool.h>

#include "common/ring_buffer_posix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETAUDIO_PREFIX "TCP:"
#define NETAUDIO_VERSION 3
#define NETAUDIO_RATE 48000
#define NETAUDIO_HELLO "MERCURY-NETAUDIO"
#define NETAUDIO_TOKEN_ENV "MERCURY_NETAUDIO_TOKEN"

/* True if devstr names a socket audio device. */
bool netaudio_is_dev(const char *devstr);

/* Connect to the host at devstr and authenticate (blocking, at most 3 s).
 * Returns false (and prints why) if devstr is not a loopback address, the
 * token is missing, or the connection/authentication fails. */
bool netaudio_open(const char *devstr);

/* True while an authenticated connection is open. */
bool netaudio_active(void);

/* Thread bodies: 'R' samples -> capture buffer (and 'D' bookkeeping);
 * playback buffer -> paced 'T' messages.  Both set shutdown_ when the
 * connection is lost. */
void *netaudio_capture_thread(void *capture_cbuf);
void *netaudio_playback_thread(void *playback_cbuf);

/* Send 'E' for the current transmission and block until the host answers 'D'
 * for it and the transmission's duration has passed, or the transmission's
 * length + 2 s (wall clock from its first sample) passed.  Returns true if
 * 'D' arrived in time; on false the connection is marked broken. */
bool netaudio_flush(void);

/* Close the connection (wakes the capture thread). */
void netaudio_close(void);

#ifdef __cplusplus
}
#endif
