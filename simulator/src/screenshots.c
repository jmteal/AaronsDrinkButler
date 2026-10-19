/* Headless screenshot script: drives the UI through every screen with a
 * fixed clock and a sample pantry/favorites, and saves PNGs. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "lodepng.h"
#include "sim.h"

bool sim_save_png(const char *path)
{
    /* The splash lives on the top layer and covers the whole screen. */
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *scr = lv_obj_get_child_count(top) ? top : lv_screen_active();
    lv_obj_update_layout(scr);
    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB888);
    if (!snap) return false;
    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    unsigned char *rgb = malloc((size_t)w * h * 3);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = snap->data + y * stride;
        for (uint32_t x = 0; x < w; x++) { /* LVGL RGB888 is stored B, G, R */
            rgb[(y * w + x) * 3 + 0] = src[x * 3 + 2];
            rgb[(y * w + x) * 3 + 1] = src[x * 3 + 1];
            rgb[(y * w + x) * 3 + 2] = src[x * 3 + 0];
        }
    }
    lv_draw_buf_destroy(snap);
    /* encode in memory: LVGL's lodepng routes file I/O through lv_fs */
    unsigned char *png = NULL;
    size_t png_len = 0;
    unsigned err = lodepng_encode24(&png, &png_len, rgb, w, h);
    free(rgb);
    if (err) {
        fprintf(stderr, "png %s: %s\n", path, lodepng_error_text(err));
        return false;
    }
    FILE *fp = fopen(path, "wb");
    bool ok = fp && fwrite(png, 1, png_len, fp) == png_len;
    if (fp) ok &= fclose(fp) == 0;
    lv_free(png);
    if (!ok) fprintf(stderr, "cannot write %s\n", path);
    return ok;
}

static void settle(void)
{
    for (int i = 0; i < 5; i++) {
        lv_timer_handler();
        lv_refr_now(NULL);
    }
}

/* Let animations and delayed deletes run (the screenshot tick is real time). */
static void wait_ms(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        lv_timer_handler();
        usleep(10000);
    }
    settle();
}

static int s_fail;
static const char *s_dir;

static void shot(const char *name)
{
    settle();
    char path[600];
    snprintf(path, sizeof path, "%s/%s.png", s_dir, name);
    if (sim_save_png(path)) printf("  %s\n", path);
    else s_fail++;
}

static void set_ids(const adb_db_t *db, uint8_t *set, bool recipes, const char *const *ids)
{
    for (; *ids; ids++) {
        int i = recipes ? adb_db_find_recipe(db, *ids) : adb_db_find_ingredient(db, *ids);
        if (i >= 0) adb_bit_set(set, (unsigned)i, true);
    }
}

int sim_take_screenshots(const char *dir, adb_db_t *db, adb_state_t *st)
{
    s_dir = dir;
    mkdir(dir, 0755);
    g_sim.quiet = true;
    g_sim.fixed_time = true;
    g_sim.time = (struct tm){ .tm_year = 126, .tm_mon = 9, .tm_mday = 18, .tm_hour = 19, .tm_min = 42, .tm_yday = 290 };
    srand(7);

    /* sample user state */
    static const char *const favs[] = { "negroni", "daiquiri", "espresso_martini", "mimosa", NULL };
    static const char *const bar[] = { "gin", "campari", "sweet_vermouth", "bourbon_whiskey", "sugar_cube",
                                       "angostura_bitters", "rum_white", "lime_juice", "ginger_ale", "grenadine",
                                       "vodka", "coffee_liqueur", "orange_juice", "lemon_juice", NULL };
    set_ids(db, st->favorites, true, favs);
    set_ids(db, st->pantry, false, bar);

    static adb_storage_t io;
    static adb_storage_mem_t mem;
    adb_storage_mem_init(&io, &mem);
    static ui_platform_t plat;
    static ui_image_provider_t img;
    sim_platform_init(&plat, &img);
    ui_config_t cfg = { .db = db, .state = st, .storage = &io, .platform = &plat, .images = &img };
    ui_init(&cfg);

    printf("Writing screenshots:\n");
    shot("01_home");

    ui_show_tab(UI_TAB_BROWSE);
    shot("02_browse");

    adb_filter_t *f = ui_browse_filter();
    adb_filter_set_tag(f, ADB_TAG_FLAVOR, adb_tag_lookup(ADB_TAG_FLAVOR, "sour"), true);
    int lime = adb_db_find_ingredient(db, "lime_juice");
    if (lime >= 0) adb_filter_set_ing(f, (uint16_t)lime, 1);
    ui_browse_apply_filter();
    shot("03_browse_filtered");

    ui_filter_set_expanded(ADB_TAG_ALCOHOLIC, false);
    ui_filter_set_expanded(ADB_TAG_SPIRIT, false);
    ui_open_filter();
    shot("04_filter_sheet");
    ui_filter_set_expanded(ADB_TAG_ALCOHOLIC, false);
    ui_filter_set_expanded(ADB_TAG_SPIRIT, false);
    ui_filter_set_expanded(ADB_TAG_FLAVOR, false);
    ui_filter_set_expanded(ADB_TAG_COUNT, true);
    int vodka = adb_db_find_ingredient(db, "vodka");
    if (vodka >= 0) adb_filter_set_ing(f, (uint16_t)vodka, -1);
    ui_close_overlays();
    ui_open_filter();
    ui_scroll_overlay_to(9 * 48 - 8); /* nine collapsed section headers */
    shot("05_filter_sheet_ingredients");
    ui_close_overlays();
    adb_filter_clear(f);
    ui_filter_set_expanded(ADB_TAG_ALCOHOLIC, true);
    ui_filter_set_expanded(ADB_TAG_SPIRIT, true);
    ui_filter_set_expanded(ADB_TAG_FLAVOR, true);
    ui_filter_set_expanded(ADB_TAG_COUNT, false);
    ui_browse_apply_filter();

    ui_open_search("coffee");
    shot("06_search_keyboard");
    ui_close_overlays();

    int neg = adb_db_find_recipe(db, "negroni");
    ui_open_detail(neg >= 0 ? (uint16_t)neg : 0);
    shot("07_detail");
    ui_close_overlays();
    int ws = adb_db_find_recipe(db, "whiskey_sour");
    ui_open_detail(ws >= 0 ? (uint16_t)ws : 0);
    ui_scroll_overlay_to(170);
    shot("08_detail_scrolled");
    ui_close_overlays();

    ui_show_tab(UI_TAB_MYBAR);
    ui_mybar_show(false, 0);
    shot("09_mybar_bottles");
    ui_mybar_show(true, 0);
    shot("10_mybar_can_make");
    ui_mybar_show(true, 1);
    shot("11_mybar_missing_1");
    ui_mybar_show(true, 2);
    shot("12_mybar_missing_2");

    ui_show_tab(UI_TAB_FAVORITES);
    shot("13_favorites");

    ui_show_tab(UI_TAB_HOME);
    ui_open_settings();
    shot("14_settings");
    ui_scroll_overlay_to(-1);
    shot("15_settings_scrolled");
    ui_open_credits();
    shot("16_credits");
    ui_close_overlays();

    /* a few variants worth reviewing */
    memset(st->favorites, 0, ADB_BITSET_BYTES(db->n_recipes));
    ui_show_tab(UI_TAB_FAVORITES);
    shot("17_favorites_empty");
    int pc = adb_db_find_recipe(db, "pina_colada");
    st->settings.use_oz = false;
    ui_open_detail(pc >= 0 ? (uint16_t)pc : 0);
    shot("18_detail_ml");
    ui_close_overlays();
    st->settings.use_oz = true;

    /* owner name + splash */
    ui_show_tab(UI_TAB_HOME);
    for (int i = 0; i < UI_LOGO_COUNT; i++) {
        char name[32];
        st->settings.next_logo = (uint8_t)i;
        ui_show_splash(0);
        snprintf(name, sizeof name, "19_splash_logo_%d", i + 1);
        shot(name);
    }
    ui_hide_splash();
    wait_ms(400);
    ui_open_settings();
    shot("20_settings_owner");
    adb_owner_set(&st->settings, "James");
    ui_open_owner_editor();
    shot("21_owner_editor");
    ui_close_overlays();
    ui_owner_changed();
    shot("22_home_named");
    ui_show_splash(0);
    shot("23_splash_named");
    ui_hide_splash();
    wait_ms(400);
    adb_owner_set(&st->settings, "Maximilian Alexander");
    ui_owner_changed();
    shot("24_home_long_name");
    adb_owner_set(&st->settings, "");
    ui_owner_changed();
    adb_storage_mem_free(&mem);
    return s_fail;
}

static uint32_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

int sim_scroll_bench(adb_db_t *db, adb_state_t *st)
{
    g_sim.quiet = true;
    static ui_platform_t plat;
    static ui_image_provider_t img;
    sim_platform_init(&plat, &img);
    ui_config_t cfg = { .db = db, .state = st, .storage = NULL, .platform = &plat, .images = &img };
    ui_init(&cfg);
    ui_show_tab(UI_TAB_BROWSE);
    settle();
    lv_obj_t *list = ui_browse_list();
    uint32_t total = db->n_recipes * 96, frames = 0, worst = 0, t_all = now_us();
    lv_obj_t *scr = lv_screen_active();
    uint32_t children = lv_obj_get_child_count(list), flushes0 = g_sim.flushes;
    for (int32_t y = 0; y < (int32_t)total; y += 24) { /* ~24 px per frame = brisk flick */
        uint32_t t0 = now_us();
        lv_obj_scroll_to_y(list, y, LV_ANIM_OFF);
        lv_obj_invalidate(scr);
        lv_refr_now(NULL);
        uint32_t dt = now_us() - t0;
        if (dt > worst) worst = dt;
        frames++;
    }
    t_all = now_us() - t_all;
    printf("Scroll bench: %u drinks, %u row objects alive (%u list children), %u frames (%u flushed), "
           "avg %.2f ms, worst %.2f ms per full-screen frame (host CPU)\n",
           db->n_recipes, ui_list_pool_size(list), children, frames, g_sim.flushes - flushes0,
           frames ? (double)t_all / frames / 1000.0 : 0.0, worst / 1000.0);
    return 0;
}

/* ---- Touch self-test ---------------------------------------------------- */

static lv_point_t s_touch_pt;
static bool s_touch_down;

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point = s_touch_pt;
    data->state = s_touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void pump(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

static void tap(int x, int y)
{
    s_touch_pt.x = x;
    s_touch_pt.y = y;
    s_touch_down = true;
    pump(80);
    s_touch_down = false;
    pump(300);
}

#define EXPECT(c) do { if (!(c)) { printf("  FAIL: %s\n", #c); fails++; } else printf("  ok: %s\n", #c); } while (0)

int sim_selftest(adb_db_t *db, adb_state_t *st)
{
    int fails = 0;
    g_sim.quiet = true;
    static adb_storage_t io;
    static adb_storage_mem_t mem;
    adb_storage_mem_init(&io, &mem);
    static ui_platform_t plat;
    static ui_image_provider_t img;
    sim_platform_init(&plat, &img);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    ui_config_t cfg = { .db = db, .state = st, .storage = &io, .platform = &plat, .images = &img };
    ui_init(&cfg);
    pump(100);

    uint16_t sorted[ADB_MAX_RECIPES];
    adb_filter_t all;
    adb_filter_clear(&all);
    size_t n = adb_filter_apply(db, &all, sorted, ADB_MAX_RECIPES);
    adb_sort_by_name(db, sorted, n);
    uint16_t first = sorted[0];

    printf("Touch self-test:\n");
    tap(120, 452);                              /* Browse tab */
    tap(200, 150);                              /* first row -> detail */
    tap(288, 56);                               /* heart */
    EXPECT(adb_bit_get(st->favorites, first));
    size_t len = 4096;
    char buf[4096];
    EXPECT(io.read(io.ctx, "favorites", buf, &len) && len > 0);
    tap(30, 56);                                /* back */
    tap(280, 452);                              /* Favorites tab */
    tap(200, 24 + 52 + 48);                     /* first favorite -> detail */
    tap(288, 56);                               /* un-heart */
    EXPECT(!adb_bit_get(st->favorites, first));
    tap(30, 56);

    tap(200, 452);                              /* My Bar */
    uint16_t ing = ADB_ING_NONE;
    for (int j = 0; j < db->n_ingredients && ing == ADB_ING_NONE; j++) {
        const adb_ingredient_t *in = &db->ingredients[db->ing_by_category[j]];
        if (in->n_recipes && in->category != ADB_CAT_GARNISH && strcmp(in->id, "water")) ing = db->ing_by_category[j];
    }
    tap(160, 24 + 56 + 48 + 24);                /* first ingredient row (below the category header) */
    EXPECT(ing != ADB_ING_NONE && adb_bit_get(st->pantry, ing));
    tap(160, 24 + 56 + 48 + 24);                /* untick */
    EXPECT(ing != ADB_ING_NONE && !adb_bit_get(st->pantry, ing));

    tap(40, 452);                               /* Home */
    tap(280, 24 + 6 + 22);                      /* gear -> settings */
    bool splash = st->settings.splash_on;
    tap(267, 207);                              /* startup splash switch */
    EXPECT(st->settings.splash_on != splash);
    tap(160, 155);                              /* owner name -> editor */
    tap(30, 50);                                /* back, unchanged */
    EXPECT(st->settings.owner[0] == 0 && adb_owner_is_default(&st->settings));
    ui_scroll_overlay_to(300);                  /* Owner card pushes Sound down 212 px */
    pump(50);
    bool sound = st->settings.sound_on;
    tap(267, 399 + 212 - 300);                  /* sound switch */
    EXPECT(st->settings.sound_on != sound);
    tap(30, 50);                                /* back */
    tap(85, 24 + 190 + 34);                     /* What can I make? -> My Bar results */
    adb_storage_mem_free(&mem);
    printf("%s\n", fails ? "self-test FAILED" : "self-test passed");
    return fails;
}
