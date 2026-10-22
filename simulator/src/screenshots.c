/* Headless screenshot script: drives the UI through every screen with a
 * fixed clock and a sample pantry/favorites, and saves PNGs. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include "lodepng.h"
#include "sim.h"

/* The visible screen as tightly packed RGB (caller frees), or NULL. */
static unsigned char *snapshot_rgb(uint32_t *w_out, uint32_t *h_out)
{
    /* The splash lives on the top layer and covers the whole screen. */
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *scr = lv_obj_get_child_count(top) ? top : lv_screen_active();
    lv_obj_update_layout(scr);
    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB888);
    if (!snap) return NULL;
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
    *w_out = w;
    *h_out = h;
    return rgb;
}

static bool write_png(const char *path, const unsigned char *rgb, uint32_t w, uint32_t h)
{
    /* encode in memory: LVGL's lodepng routes file I/O through lv_fs */
    unsigned char *png = NULL;
    size_t png_len = 0;
    unsigned err = lodepng_encode24(&png, &png_len, rgb, w, h);
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

bool sim_save_png(const char *path)
{
    uint32_t w, h;
    unsigned char *rgb = snapshot_rgb(&w, &h);
    if (!rgb) return false;
    bool ok = write_png(path, rgb, w, h);
    free(rgb);
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
    tap(232, 56);                               /* speak the recipe */
    EXPECT(plat.speech_playing(plat.ctx));
    tap(232, 56);                               /* stop */
    EXPECT(!plat.speech_playing(plat.ctx));
    tap(232, 56);                               /* play again, then leave the screen */
    tap(30, 56);
    EXPECT(!plat.speech_playing(plat.ctx));     /* closing the detail stops it */

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

    printf("Screensaver and sleep:\n");
    st->settings.splash_on = true;
    st->settings.sleep_s = 600;
    tap(120, 452);                              /* Browse */
    tap(200, 150);                              /* a drink's detail stays open... */
    EXPECT(ui_overlay_open());
    pump(UI_SAVER_AFTER_S * 1000 - 3000);
    EXPECT(!ui_splash_visible());               /* ...not yet */
    pump(4500);
    EXPECT(ui_splash_visible());                /* logos are up after 4 min */
    uint8_t logo = st->settings.next_logo;
    pump(UI_SAVER_CYCLE_MS + 1000);
    EXPECT(st->settings.next_logo != logo);     /* and cycling */
    tap(160, 240);
    pump(400);
    EXPECT(!ui_splash_visible() && !ui_overlay_open() && ui_current_tab() == UI_TAB_HOME);

    tap(120, 452);                              /* Browse, then the screen goes off */
    ui_sleep_prepare();
    pump(100);
    EXPECT(ui_splash_visible());                /* wakes to a logo */
    tap(160, 240);
    pump(400);
    EXPECT(!ui_splash_visible() && ui_current_tab() == UI_TAB_HOME);

    st->settings.sleep_s = 0;                   /* "Never": no screensaver */
    pump(UI_SAVER_AFTER_S * 1000 + 2000);
    EXPECT(!ui_splash_visible());
    adb_storage_mem_free(&mem);
    printf("%s\n", fails ? "self-test FAILED" : "self-test passed");
    return fails;
}

/* ---- Demo recording ----------------------------------------------------- */

#define REC_FPS     15
#define REC_TOUCH_R 16      /* touch marker radius, px */
#define REC_FADE_MS 300     /* marker fades out after a release */

static uint32_t s_rec_ms, s_rec_frames, s_rec_up_ms;
static bool s_rec_seen;     /* a touch happened, so there is a marker to fade */
static int s_rec_fail;
static FILE *s_rec_events;

/* Blends a soft white disc with a darker rim over the frame at the touch point. */
static void draw_touch(unsigned char *rgb, uint32_t w, uint32_t h)
{
    float a;
    if (s_touch_down) a = 1.0f;
    else if (s_rec_seen && s_rec_ms - s_rec_up_ms < REC_FADE_MS) a = 1.0f - (float)(s_rec_ms - s_rec_up_ms) / REC_FADE_MS;
    else return;
    float r = REC_TOUCH_R + (s_touch_down ? 0.0f : 8.0f * (1.0f - a)); /* grows a little as it fades */
    int cx = s_touch_pt.x, cy = s_touch_pt.y;
    for (int y = cy - (int)r - 2; y <= cy + (int)r + 2; y++) {
        if (y < 0 || y >= (int)h) continue;
        for (int x = cx - (int)r - 2; x <= cx + (int)r + 2; x++) {
            if (x < 0 || x >= (int)w) continue;
            float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float cover = r + 0.5f - d;                 /* anti-aliased edge */
            if (cover <= 0) continue;
            if (cover > 1) cover = 1;
            bool rim = d > r - 2.5f;
            float op = cover * a * (rim ? 0.75f : 0.40f);
            unsigned char c = rim ? 0x40 : 0xFF;
            unsigned char *p = &rgb[((size_t)y * w + (size_t)x) * 3];
            for (int k = 0; k < 3; k++) p[k] = (unsigned char)(p[k] * (1 - op) + c * op);
        }
    }
}

static void rec_frame(void)
{
    uint32_t w, h;
    unsigned char *rgb = snapshot_rgb(&w, &h);
    if (!rgb) { s_rec_fail++; return; }
    draw_touch(rgb, w, h);
    char path[600];
    snprintf(path, sizeof path, "%s/frame_%05u.png", s_dir, s_rec_frames);
    if (!write_png(path, rgb, w, h)) s_rec_fail++;
    free(rgb);
    s_rec_frames++;
}

/* Advances the UI clock in 10 ms steps, writing a frame every 1/REC_FPS s. */
static void rec_pump(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
        s_rec_ms += 10;
        while (s_rec_frames * 1000 / REC_FPS <= s_rec_ms) rec_frame();
    }
}

static void rec_press(int x, int y)
{
    s_touch_pt.x = x;
    s_touch_pt.y = y;
    s_touch_down = true;
    s_rec_seen = true;
}

static void rec_release(void)
{
    s_touch_down = false;
    s_rec_up_ms = s_rec_ms;
}

/* A tap, then `after` ms to watch what it did. The finger stays up for at
 * least 100 ms so LVGL's input polling sees the release. */
static void rec_tap(int x, int y, int after)
{
    rec_press(x, y);
    rec_pump(150);
    rec_release();
    rec_pump(after < 100 ? 100 : after);
}

/* A finger drag from (x0, y0) to (x1, y1) over ms; LVGL adds the momentum. */
static void rec_drag(int x0, int y0, int x1, int y1, int ms, int after)
{
    rec_press(x0, y0);
    rec_pump(60);
    for (int t = 10; t <= ms; t += 10) {
        s_touch_pt.x = x0 + (x1 - x0) * t / ms;
        s_touch_pt.y = y0 + (y1 - y0) * t / ms;
        rec_pump(10);
    }
    rec_release();
    rec_pump(after < 100 ? 100 : after);
}

/* The last visible label (topmost in draw order) whose text is `text`, or
 * contains it when `part`. */
static lv_obj_t *find_label(lv_obj_t *o, const char *text, bool part, lv_obj_t *best)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return best;
    if (lv_obj_check_type(o, &lv_label_class) && lv_obj_is_visible(o)) {
        const char *t = lv_label_get_text(o);
        if (t && (part ? strstr(t, text) != NULL : !strcmp(t, text))) best = o;
    }
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) best = find_label(lv_obj_get_child(o, (int32_t)i), text, part, best);
    return best;
}

static lv_obj_t *visible_label(const char *text, bool part)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    return find_label(lv_layer_top(), text, part, find_label(scr, text, part, NULL));
}

/* Flicks a list up until the label showing `text` is well inside it. */
static void rec_scroll_to_label(const char *text)
{
    for (int i = 0; i < 8; i++) {
        lv_obj_t *l = visible_label(text, false);
        lv_area_t a;
        if (l) lv_obj_get_coords(l, &a);
        if (l && a.y2 < 330) return;
        rec_drag(160, 380, 160, 220, 300, 900);
    }
}

/* Taps the centre of the label showing `text`, like a finger would. */
static void rec_tap_label(const char *text, bool part, int after)
{
    lv_obj_t *l = visible_label(text, part);
    if (!l) {
        fprintf(stderr, "record: no visible label \"%s\"\n", text);
        s_rec_fail++;
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(l, &a);
    rec_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, after);
}

int sim_record(const char *dir, adb_db_t *db, adb_state_t *st)
{
    s_dir = dir;
    mkdir(dir, 0755);
    char path[600];
    snprintf(path, sizeof path, "%s/events.txt", dir);
    s_rec_events = fopen(path, "w");
    if (!s_rec_events) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    g_sim.quiet = true;
    g_sim.fixed_time = true;
    g_sim.time = (struct tm){ .tm_year = 126, .tm_mon = 9, .tm_mday = 18, .tm_hour = 19, .tm_min = 42, .tm_yday = 290 };
    srand(7);

    /* the same sample user state as the screenshots */
    static const char *const favs[] = { "negroni", "daiquiri", "espresso_martini", "mimosa", NULL };
    static const char *const bar[] = { "gin", "campari", "sweet_vermouth", "bourbon_whiskey", "sugar_cube",
                                       "angostura_bitters", "rum_white", "lime_juice", "ginger_ale", "grenadine",
                                       "vodka", "coffee_liqueur", "orange_juice", "lemon_juice", NULL };
    set_ids(db, st->favorites, true, favs);
    set_ids(db, st->pantry, false, bar);
    st->settings.sound_on = true;

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

    printf("Recording the tour into %s\n", dir);
    rec_pump(1500);                                 /* Home */

    rec_tap(120, 452, 900);                         /* Browse */
    rec_drag(160, 400, 160, 160, 300, 1400);        /* flick down the list */
    rec_drag(160, 400, 160, 200, 300, 1200);
    rec_drag(160, 150, 160, 450, 250, 1000);        /* and back up */
    rec_drag(160, 150, 160, 450, 250, 1200);

    rec_tap_label("Filter", true, 1000);            /* filter sheet */
    rec_tap_label("Type", false, 700);              /* collapse two sections */
    rec_tap_label("Spirit", false, 700);
    rec_tap_label("Bitter", false, 700);            /* require "bitter"... */
    rec_tap_label("Spirit-forward", false, 900);    /* ...and "spirit-forward" */
    rec_tap_label("Show ", true, 1200);             /* back to the filtered list */

    rec_scroll_to_label("Negroni");
    rec_tap_label("Negroni", false, 1800);          /* detail */
    rec_press(232, 56);                             /* speak the recipe: stop icon while it plays */
    rec_pump(150);
    rec_release();                                  /* the clip starts on release */
    fprintf(s_rec_events, "speak_ms %u\n", s_rec_ms);
    rec_pump(1200);
    rec_drag(160, 420, 160, 200, 500, 0);           /* read along */
    while (plat.speech_playing(plat.ctx)) rec_pump(100);
    rec_pump(800);
    rec_tap(30, 56, 900);                           /* back */

    rec_tap(200, 452, 1200);                        /* My Bar: the bottles checklist */
    rec_tap_label("What can I make?", false, 1800); /* what the pantry makes */
    rec_tap_label("Missing 1", true, 1500); 

    rec_tap(280, 452, 1800);                        /* Favorites */

    rec_tap(40, 452, 900);                          /* Home */
    rec_tap(280, 24 + 6 + 22, 1500);                /* gear -> Settings */
    rec_drag(160, 400, 160, 160, 400, 1500);
    rec_tap(30, 50, 1200);                          /* back to Home */

    fprintf(s_rec_events, "fps %d\nframes %u\nduration_ms %u\n", REC_FPS, s_rec_frames, s_rec_ms);
    fclose(s_rec_events);
    adb_storage_mem_free(&mem);
    printf("record: %u frames (%.1f s)%s\n", s_rec_frames, s_rec_ms / 1000.0, s_rec_fail ? ", with failures" : "");
    return s_rec_fail;
}
