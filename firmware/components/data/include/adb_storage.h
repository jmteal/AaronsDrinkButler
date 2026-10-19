/*
 * Persistence of user state (favorites, pantry, settings) behind a tiny
 * key/value interface. Desktop: one file per key in a directory
 * (adb_storage_file_init). Device: NVS blobs (implemented by the firmware).
 *
 * Favorites and pantry are saved as newline-separated stable IDs, not as
 * raw bitsets, so they survive dataset updates that reorder or add drinks.
 */
#ifndef ADB_STORAGE_H
#define ADB_STORAGE_H

#include "adb_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct adb_storage {
    /* Read up to *len bytes; on success set *len to the stored size and return true. */
    bool (*read)(void *ctx, const char *key, void *buf, size_t *len);
    bool (*write)(void *ctx, const char *key, const void *buf, size_t len);
    void *ctx;
} adb_storage_t;

#define ADB_OWNER_MAX     24          /* bytes, without the terminator */
#define ADB_OWNER_DEFAULT "Aaron"

typedef struct {
    uint8_t  brightness;      /* 5..100 % */
    uint16_t sleep_s;         /* screen-off timeout, 0 = never */
    bool     sound_on;
    uint8_t  volume;          /* 0..100 */
    bool     use_oz;          /* units on the detail screen */
    char     owner[ADB_OWNER_MAX + 1];   /* UTF-8; empty = ADB_OWNER_DEFAULT */
    bool     splash_on;       /* show the splash screen at startup */
    uint8_t  next_logo;       /* default owner: which logo the next splash shows */
} adb_settings_t;

typedef struct {
    uint8_t *favorites;       /* recipe bitset */
    uint8_t *pantry;          /* ingredient bitset */
    adb_settings_t settings;
} adb_state_t;

void adb_settings_default(adb_settings_t *s);

/* Owner name helpers. The name is trimmed; blank means ADB_OWNER_DEFAULT. */
const char *adb_owner_name(const adb_settings_t *s);
/* True when the owner is the default (Aaron, any case), who gets the logos. */
bool adb_owner_is_default(const adb_settings_t *s);
/* Store a new owner name (trimmed, cut to ADB_OWNER_MAX on a UTF-8 boundary). */
void adb_owner_set(adb_settings_t *s, const char *name);
/* "Aaron's" / "James'" into buf; upper = ASCII upper case ("AARON'S"). */
void adb_owner_possessive(const adb_settings_t *s, char *buf, size_t n, bool upper);

/* Allocates bitsets sized for db and sets defaults. */
bool adb_state_init(adb_state_t *st, const adb_db_t *db);
void adb_state_free(adb_state_t *st);

/* Load/save everything. Missing keys leave defaults. Unknown IDs are dropped. */
void adb_state_load(adb_state_t *st, const adb_db_t *db, const adb_storage_t *io);
bool adb_state_save_favorites(const adb_state_t *st, const adb_db_t *db, const adb_storage_t *io);
bool adb_state_save_pantry(const adb_state_t *st, const adb_db_t *db, const adb_storage_t *io);
bool adb_state_save_settings(const adb_state_t *st, const adb_storage_t *io);

/* Desktop backend: files under dir (created if missing). ctx points into *backing. */
typedef struct { char dir[256]; } adb_storage_file_t;
bool adb_storage_file_init(adb_storage_t *io, adb_storage_file_t *backing, const char *dir);

/* In-memory backend for tests. */
typedef struct { char key[8][24]; uint8_t *val[8]; size_t len[8]; int n; } adb_storage_mem_t;
void adb_storage_mem_init(adb_storage_t *io, adb_storage_mem_t *backing);
void adb_storage_mem_free(adb_storage_mem_t *backing);

#ifdef __cplusplus
}
#endif
#endif
