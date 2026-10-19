/* Desktop storage backend: one file per key. Not built for ESP-IDF. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "adb_storage.h"

static void key_path(const adb_storage_file_t *f, const char *key, char *out, size_t len)
{
    snprintf(out, len, "%s/%s.dat", f->dir, key);
}

static bool file_read(void *ctx, const char *key, void *buf, size_t *len)
{
    char path[320];
    key_path(ctx, key, path, sizeof path);
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    size_t n = fread(buf, 1, *len, fp);
    bool more = fgetc(fp) != EOF; /* buffer too small */
    fclose(fp);
    if (more) return false;
    *len = n;
    return true;
}

static bool file_write(void *ctx, const char *key, const void *buf, size_t len)
{
    char path[320], tmp[330];
    key_path(ctx, key, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return false;
    bool ok = fwrite(buf, 1, len, fp) == len;
    ok &= fclose(fp) == 0;
    return ok && rename(tmp, path) == 0;
}

bool adb_storage_file_init(adb_storage_t *io, adb_storage_file_t *backing, const char *dir)
{
    snprintf(backing->dir, sizeof(backing->dir), "%s", dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    io->read = file_read;
    io->write = file_write;
    io->ctx = backing;
    return true;
}
