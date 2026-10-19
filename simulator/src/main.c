/*
 * Aaron's Drink Butler desktop simulator.
 *
 *   drink_butler_sim [--data recipes.json] [--state DIR] [--zoom N]
 *   drink_butler_sim --check [--data recipes.json]   validate a dataset, print warnings
 *   drink_butler_sim --screenshots DIR               headless: PNG of every screen
 *   drink_butler_sim --bench [--data recipes.json]   headless: Browse scroll timing
 *   drink_butler_sim --selftest                      headless: scripted taps, checks state
 *
 * Default dataset: data/recipes.json if present, else docs/research/sample_recipes.json.
 * Mouse = touch. Favorites/pantry/settings persist in ~/.drink_butler_sim/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "lvgl.h"
#include "adb_load.h"
#include "sim.h"

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--data recipes.json] [--state DIR] [--zoom N]\n"
            "       %s --check [--data recipes.json]\n"
            "       %s --screenshots DIR [--data recipes.json]\n"
            "       %s --bench [--data recipes.json]\n"
            "       %s --selftest\n", argv0, argv0, argv0, argv0, argv0);
}

static bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static uint32_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void headless_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    g_sim.flushes++;
    lv_display_flush_ready(d);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *state_dir = NULL, *shots = NULL;
    bool check = false, bench = false, selftest = false;
    float zoom = 1.0f;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) state_dir = argv[++i];
        else if (!strcmp(argv[i], "--screenshots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--bench")) bench = true;
        else if (!strcmp(argv[i], "--selftest")) selftest = true;
        else { usage(argv[0]); return 2; }
    }

    static char def_data[600];
    if (!data) {
        snprintf(def_data, sizeof def_data, "%s/data/recipes.json", ADB_REPO_DIR);
        if (!exists(def_data)) snprintf(def_data, sizeof def_data, "%s/docs/research/sample_recipes.json", ADB_REPO_DIR);
        data = def_data;
    }

    static adb_db_t db;
    adb_db_init(&db);
    int rc = adb_load_json_file(&db, data, NULL);
    printf("Loaded %u drinks, %u ingredients from %s\n", db.n_recipes, db.n_ingredients, data);
    if (db.n_warnings) {
        printf("%u warning(s):\n", db.n_warnings);
        for (int i = 0; i < db.n_warnings && i < 16; i++) printf("  %s\n", db.warnings[i]);
        if (db.n_warnings > 16) printf("  ... and %u more\n", db.n_warnings - 16);
    }
    if (rc != 0) return 1;
    if (check) return db.n_warnings ? 3 : 0;

    static adb_state_t st;
    adb_state_init(&st, &db);

    lv_init();
    srand((unsigned)time(NULL));

    if (shots || bench || selftest) {
        if (!selftest) lv_tick_set_cb(mono_ms); /* the self-test advances time itself */
        lv_display_t *disp = lv_display_create(UI_HOR_RES, UI_VER_RES);
        size_t sz = UI_HOR_RES * UI_VER_RES * 2;
        void *buf = malloc(sz);
        lv_display_set_buffers(disp, buf, NULL, (uint32_t)sz, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(disp, headless_flush);
        if (bench) return sim_scroll_bench(&db, &st);
        if (selftest) return sim_selftest(&db, &st) ? 1 : 0;
        int fails = sim_take_screenshots(shots, &db, &st);
        printf("%s\n", fails ? "screenshots: some failed" : "screenshots: done");
        return fails ? 1 : 0;
    }

#if !LV_USE_SDL
    (void)state_dir;
    (void)zoom;
    fprintf(stderr, "This is a headless build: use --screenshots, --selftest or --bench.\n");
    return 2;
#else
    static adb_storage_t io;
    static adb_storage_file_t fio;
    char dir[512];
    if (!state_dir) {
        const char *home = getenv("HOME");
        snprintf(dir, sizeof dir, "%s/.drink_butler_sim", home ? home : ".");
        state_dir = dir;
    }
    bool have_io = adb_storage_file_init(&io, &fio, state_dir);
    if (have_io) adb_state_load(&st, &db, &io);
    printf("State in %s\n", state_dir);

    lv_display_t *disp = lv_sdl_window_create(UI_HOR_RES, UI_VER_RES);
    lv_sdl_window_set_title(disp, "Aaron's Drink Butler");
    if (zoom > 0.5f && zoom != 1.0f) lv_sdl_window_set_zoom(disp, zoom);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();

    static ui_platform_t plat;
    static ui_image_provider_t img;
    sim_platform_init(&plat, &img);
    ui_config_t cfg = { .db = &db, .state = &st, .storage = have_io ? &io : NULL, .platform = &plat, .images = &img,
                       .splash_ms = 2500 };
    ui_init(&cfg);

    while (1) {
        uint32_t wait = lv_timer_handler();
        if (wait > 10) wait = 10;
        usleep(wait * 1000);
    }
    return 0;
#endif
}
