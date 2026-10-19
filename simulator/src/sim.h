/* Simulator glue: platform stubs, image provider, screenshot script. */
#ifndef ADB_SIM_H
#define ADB_SIM_H

#include "ui.h"

typedef struct {
    int  battery_pct;
    int  battery_mv;
    bool charging;
    uint8_t brightness;
    bool fixed_time;      /* screenshots: use a fixed clock */
    struct tm time;
    bool quiet;           /* don't log sounds / hardware calls */
    uint32_t flushes;     /* headless display flush count */
} sim_platform_state_t;

extern sim_platform_state_t g_sim;

void sim_platform_init(ui_platform_t *p, ui_image_provider_t *img);

/* Writes PNGs of every screen into dir. Returns number of failures. */
int sim_take_screenshots(const char *dir, adb_db_t *db, adb_state_t *st);

/* Scrolls Browse from top to bottom, rendering every step; prints timing. */
int sim_scroll_bench(adb_db_t *db, adb_state_t *st);

/* Drives the UI with scripted taps through an LVGL pointer indev and checks
 * the resulting state. Returns number of failures. */
int sim_selftest(adb_db_t *db, adb_state_t *st);

/* Saves the active screen as a PNG. */
bool sim_save_png(const char *path);

#endif
