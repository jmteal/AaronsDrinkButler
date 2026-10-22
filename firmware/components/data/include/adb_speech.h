/*
 * Spoken recipe clips: speech/<drink id>.opk in the recipe pack, written by
 * tools/data/adb/audio.py. Mono Opus packets, decoded at 16 kHz.
 *
 * Format (little-endian):
 *   char magic[4] "ADBS", u16 version, u16 pre_skip (samples at 16 kHz to drop
 *   from the start of the decoded audio), u32 samples (16 kHz, after pre_skip),
 *   u32 packet count, then per packet: u16 length, the packet bytes
 *
 * Only parsing lives here (host and device); decoding is the firmware's job.
 */
#ifndef ADB_SPEECH_H
#define ADB_SPEECH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "adb_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADB_SPEECH_VERSION     1
#define ADB_SPEECH_RATE        16000
#define ADB_SPEECH_NAME_FMT    "speech/%s.opk"

typedef struct {
    const uint8_t *p, *end;    /* next packet's length field, end of the clip */
    uint16_t pre_skip;
    uint32_t samples;
    uint32_t packets;          /* in the clip */
    uint32_t next;             /* packets read so far */
} adb_speech_t;

/* Finds speech/<drink_id>.opk in the pack and opens it. 0 on success, negative
 * if it is missing (-1) or malformed (-2). s may be NULL to just test for it. */
int adb_speech_find(adb_speech_t *s, const adb_pack_t *p, const char *drink_id);

/* Opens a clip held in memory. 0 on success. */
int adb_speech_open(adb_speech_t *s, const void *blob, size_t len);

/* Next packet: its length (> 0) with *pkt pointing at it, 0 at the end, or -1
 * if the clip is truncated. */
int adb_speech_next(adb_speech_t *s, const uint8_t **pkt);

/* Clip length in milliseconds. */
static inline uint32_t adb_speech_ms(const adb_speech_t *s)
{
    return (uint32_t)((uint64_t)s->samples * 1000 / ADB_SPEECH_RATE);
}

#ifdef __cplusplus
}
#endif
#endif
