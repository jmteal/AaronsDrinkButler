#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adb_filter.h"
#include "adb_storage.h"

#define KEY_FAV      "favorites"
#define KEY_PANTRY   "pantry"
#define KEY_SETTINGS "settings"
#define SETTINGS_VER 1

void adb_settings_default(adb_settings_t *s)
{
    s->brightness = 60;
    s->sleep_s = 120;
    s->sound_on = true;
    s->volume = 60;
    s->use_oz = true;
}

bool adb_state_init(adb_state_t *st, const adb_db_t *db)
{
    memset(st, 0, sizeof(*st));
    st->favorites = calloc(1, ADB_BITSET_BYTES(db->n_recipes) + 1);
    st->pantry = calloc(1, ADB_BITSET_BYTES(db->n_ingredients) + 1);
    adb_settings_default(&st->settings);
    return st->favorites && st->pantry;
}

void adb_state_free(adb_state_t *st)
{
    free(st->favorites);
    free(st->pantry);
    memset(st, 0, sizeof(*st));
}

/* id list helpers: "a\nb\nc\n" */
typedef int (*find_fn)(const adb_db_t *, const char *);

static void parse_ids(char *buf, size_t len, const adb_db_t *db, find_fn find, uint8_t *set)
{
    buf[len] = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "\n", &save); tok; tok = strtok_r(NULL, "\n", &save)) {
        int i = find(db, tok);
        if (i >= 0) adb_bit_set(set, (unsigned)i, true);
    }
}

static bool load_list(const adb_storage_t *io, const char *key, const adb_db_t *db, find_fn find, uint8_t *set)
{
    size_t cap = 16384, len = cap - 1;
    char *buf = malloc(cap);
    if (!buf) return false;
    bool ok = io->read(io->ctx, key, buf, &len) && len < cap;
    if (ok) parse_ids(buf, len, db, find, set);
    free(buf);
    return ok;
}

static bool save_list(const adb_storage_t *io, const char *key, const uint8_t *set, int n,
                      const char *(*id_of)(const adb_db_t *, int), const adb_db_t *db)
{
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) return false;
    for (int i = 0; i < n; i++) {
        if (!adb_bit_get(set, (unsigned)i)) continue;
        const char *id = id_of(db, i);
        size_t l = strlen(id);
        if (len + l + 2 > cap) {
            cap = (len + l + 2) * 2;
            char *p = realloc(buf, cap);
            if (!p) { free(buf); return false; }
            buf = p;
        }
        memcpy(buf + len, id, l);
        len += l;
        buf[len++] = '\n';
    }
    bool ok = io->write(io->ctx, key, buf, len);
    free(buf);
    return ok;
}

static const char *recipe_id(const adb_db_t *db, int i) { return db->recipes[i].id; }
static const char *ing_id(const adb_db_t *db, int i) { return db->ingredients[i].id; }

typedef struct {
    uint8_t ver, brightness, sound_on, volume, use_oz, pad;
    uint16_t sleep_s;
} settings_blob_t;

void adb_state_load(adb_state_t *st, const adb_db_t *db, const adb_storage_t *io)
{
    if (!io) return;
    load_list(io, KEY_FAV, db, adb_db_find_recipe, st->favorites);
    load_list(io, KEY_PANTRY, db, adb_db_find_ingredient, st->pantry);
    settings_blob_t b;
    size_t len = sizeof(b);
    if (io->read(io->ctx, KEY_SETTINGS, &b, &len) && len == sizeof(b) && b.ver == SETTINGS_VER) {
        st->settings.brightness = b.brightness < 5 ? 5 : (b.brightness > 100 ? 100 : b.brightness);
        st->settings.sound_on = b.sound_on;
        st->settings.volume = b.volume > 100 ? 100 : b.volume;
        st->settings.use_oz = b.use_oz;
        st->settings.sleep_s = b.sleep_s;
    }
}

bool adb_state_save_favorites(const adb_state_t *st, const adb_db_t *db, const adb_storage_t *io)
{
    return io && save_list(io, KEY_FAV, st->favorites, db->n_recipes, recipe_id, db);
}

bool adb_state_save_pantry(const adb_state_t *st, const adb_db_t *db, const adb_storage_t *io)
{
    return io && save_list(io, KEY_PANTRY, st->pantry, db->n_ingredients, ing_id, db);
}

bool adb_state_save_settings(const adb_state_t *st, const adb_storage_t *io)
{
    if (!io) return false;
    settings_blob_t b = {
        .ver = SETTINGS_VER, .brightness = st->settings.brightness,
        .sound_on = st->settings.sound_on, .volume = st->settings.volume,
        .use_oz = st->settings.use_oz, .sleep_s = st->settings.sleep_s,
    };
    return io->write(io->ctx, KEY_SETTINGS, &b, sizeof(b));
}

/* ---- In-memory backend ------------------------------------------------ */

static int mem_find(adb_storage_mem_t *m, const char *key)
{
    for (int i = 0; i < m->n; i++)
        if (strcmp(m->key[i], key) == 0) return i;
    return -1;
}

static bool mem_read(void *ctx, const char *key, void *buf, size_t *len)
{
    adb_storage_mem_t *m = ctx;
    int i = mem_find(m, key);
    if (i < 0 || m->len[i] > *len) return false;
    memcpy(buf, m->val[i], m->len[i]);
    *len = m->len[i];
    return true;
}

static bool mem_write(void *ctx, const char *key, const void *buf, size_t len)
{
    adb_storage_mem_t *m = ctx;
    int i = mem_find(m, key);
    if (i < 0) {
        if (m->n >= 8) return false;
        i = m->n++;
        snprintf(m->key[i], sizeof(m->key[i]), "%s", key);
        m->val[i] = NULL;
    }
    free(m->val[i]);
    m->val[i] = malloc(len ? len : 1);
    if (!m->val[i]) return false;
    memcpy(m->val[i], buf, len);
    m->len[i] = len;
    return true;
}

void adb_storage_mem_init(adb_storage_t *io, adb_storage_mem_t *backing)
{
    memset(backing, 0, sizeof(*backing));
    io->read = mem_read;
    io->write = mem_write;
    io->ctx = backing;
}

void adb_storage_mem_free(adb_storage_mem_t *backing)
{
    for (int i = 0; i < backing->n; i++) free(backing->val[i]);
    memset(backing, 0, sizeof(*backing));
}
