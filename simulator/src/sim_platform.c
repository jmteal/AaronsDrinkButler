/* Simulator implementations of the UI's platform and image interfaces. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "sim.h"

sim_platform_state_t g_sim = { .battery_pct = 82, .battery_mv = 3950, .brightness = 60 };

static int battery_percent(void *ctx) { return g_sim.battery_pct; }
static int battery_mv(void *ctx) { return g_sim.battery_mv; }
static bool is_charging(void *ctx) { return g_sim.charging; }

static void set_brightness(void *ctx, uint8_t pct)
{
    g_sim.brightness = pct;
    if (!g_sim.quiet) printf("[hw] brightness %u%%\n", pct);
}

static void set_sleep_timeout(void *ctx, uint16_t s)
{
    if (!g_sim.quiet) printf("[hw] screen off after %u s%s\n", s, s ? "" : " (never)");
}

static void set_sound(void *ctx, bool on, uint8_t vol)
{
    if (!g_sim.quiet) printf("[hw] sound %s, volume %u\n", on ? "on" : "off", vol);
}

static void play_sound(void *ctx, ui_sound_t s)
{
    static const char *names[] = { "tap", "toggle", "clink", "back" };
    if (!g_sim.quiet) printf("[sound] %s\n", names[s]);
}

static bool get_time(void *ctx, struct tm *out)
{
    if (g_sim.fixed_time) { *out = g_sim.time; return true; }
    time_t now = time(NULL);
    localtime_r(&now, out);
    return true;
}

/* Images: files next to the dataset ("A:" is LVGL's stdio driver). Returns
 * NULL when the file doesn't exist, so the UI draws its glass icon. */
static const void *file_src(const adb_db_t *db, const char *rel, char *buf, size_t len)
{
    if (!rel || !*rel) return NULL;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", db->data_dir, rel);
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return NULL;
    snprintf(buf, len, "A:%s", path);
    return buf;
}

static const void *thumb(void *ctx, const adb_db_t *db, uint16_t r)
{
    static char buf[520]; /* lv_image_set_src copies path strings */
    return file_src(db, db->recipes[r].image.thumb, buf, sizeof buf);
}

static const void *photo(void *ctx, const adb_db_t *db, uint16_t r)
{
    static char buf[520];
    return file_src(db, db->recipes[r].image.file, buf, sizeof buf);
}

void sim_platform_init(ui_platform_t *p, ui_image_provider_t *img)
{
    memset(p, 0, sizeof(*p));
    p->battery_percent = battery_percent;
    p->battery_mv = battery_mv;
    p->is_charging = is_charging;
    p->set_brightness = set_brightness;
    p->set_sleep_timeout = set_sleep_timeout;
    p->set_sound = set_sound;
    p->play_sound = play_sound;
    p->get_time = get_time;
    p->version = "v0.1.0 (simulator)";
    memset(img, 0, sizeof(*img));
    img->thumb = thumb;
    img->photo = photo;
}
