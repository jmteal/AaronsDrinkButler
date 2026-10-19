/* Spoken recipe clip parsing (format in adb_speech.h). */
#include "adb_speech.h"

#include <stdio.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int adb_speech_open(adb_speech_t *s, const void *blob, size_t len)
{
    const uint8_t *b = blob;
    if (!b || len < 16 || memcmp(b, "ADBS", 4) || rd16(b + 4) != ADB_SPEECH_VERSION) return -2;
    adb_speech_t t = {
        .p = b + 16, .end = b + len,
        .pre_skip = rd16(b + 6), .samples = rd32(b + 8), .packets = rd32(b + 12),
    };
    if (s) *s = t;
    return 0;
}

int adb_speech_find(adb_speech_t *s, const adb_pack_t *p, const char *drink_id)
{
    char name[ADB_PACK_NAME_MAX];
    if (!p || !drink_id || snprintf(name, sizeof name, ADB_SPEECH_NAME_FMT, drink_id) >= (int)sizeof name)
        return -1;
    size_t len = 0;
    const void *blob = adb_pack_find(p, name, &len);
    if (!blob) return -1;
    adb_speech_t t;
    return adb_speech_open(s ? s : &t, blob, len);
}

int adb_speech_next(adb_speech_t *s, const uint8_t **pkt)
{
    if (s->next >= s->packets) return 0;
    if (s->end - s->p < 2) return -1;
    uint16_t n = rd16(s->p);
    if (n == 0 || s->end - s->p - 2 < n) return -1;
    *pkt = s->p + 2;
    s->p += 2 + n;
    s->next++;
    return n;
}
