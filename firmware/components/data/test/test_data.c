/* Host unit tests: loading, filters, pantry matching, persistence, formatting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "adb_data.h"
#include "adb_filter.h"
#include "adb_load.h"
#include "adb_pack.h"
#include "adb_pantry.h"
#include "adb_storage.h"

static int g_fail, g_checks;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; if (strcmp((a), (b)) != 0) { g_fail++; \
    fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define RUN(fn) do { int before = g_fail; fn(); printf("%-36s %s\n", #fn, g_fail == before ? "ok" : "FAILED"); } while (0)

static adb_db_t g_db;

static int R(const char *id) { return adb_db_find_recipe(&g_db, id); }
static uint16_t I(const char *id) { int i = adb_db_find_ingredient(&g_db, id); return i < 0 ? ADB_ING_NONE : (uint16_t)i; }

/* Returns matches as a sorted comma-separated id string for easy comparison. */
static const char *match_ids(const adb_filter_t *f)
{
    static char buf[512];
    uint16_t idx[ADB_MAX_RECIPES];
    size_t n = adb_filter_apply(&g_db, f, idx, ADB_MAX_RECIPES);
    adb_sort_by_name(&g_db, idx, n);
    buf[0] = 0;
    for (size_t i = 0; i < n; i++) {
        if (i) strcat(buf, ",");
        strcat(buf, g_db.recipes[idx[i]].id);
    }
    return buf;
}

static void load_sample(const char *ingredients_json)
{
    adb_db_free(&g_db);
    adb_db_init(&g_db);
    FILE *fp = fopen(SAMPLE_JSON, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", SAMPLE_JSON); exit(2); }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    buf[fread(buf, 1, (size_t)n, fp)] = 0;
    fclose(fp);
    int rc = adb_load_json_buffers(&g_db, buf, ingredients_json);
    free(buf);
    if (rc != 0) { fprintf(stderr, "load failed %d\n", rc); exit(2); }
}

/* ---------------------------------------------------------------------- */

static void test_load(void)
{
    load_sample(NULL);
    CHECK(g_db.n_recipes == 10);
    for (int i = 0; i < g_db.n_warnings && i < 16; i++) fprintf(stderr, "  warning: %s\n", g_db.warnings[i]);
    CHECK(g_db.n_warnings == 0);
    int neg = R("negroni");
    CHECK(neg >= 0);
    const adb_recipe_t *r = &g_db.recipes[neg];
    CHECK_STR(r->name, "Negroni");
    CHECK(r->n_ings == 3 && r->n_steps == 4);
    CHECK(r->glass == adb_tag_lookup(ADB_TAG_GLASS, "rocks"));
    CHECK(r->color_rgb == 0xA4161A);
    CHECK_STR(adb_recipe_spirit_label(r), "Gin");
    CHECK_STR(adb_recipe_spirit_label(&g_db.recipes[R("shirley_temple")]), "Mocktail");
    CHECK(g_db.recipes[R("old_fashioned")].difficulty == 0);
    CHECK(g_db.recipes[R("margarita")].ings[3].optional);
    CHECK(g_db.recipes[R("margarita")].ings[3].amount < 0);
    /* ingredient index */
    CHECK(I("lime_juice") != ADB_ING_NONE);
    CHECK(g_db.ingredients[I("lime_juice")].n_recipes == 3);
    CHECK(g_db.ingredients[I("lime_juice")].category == ADB_CAT_JUICE);
    CHECK(g_db.ingredients[I("gin")].category == ADB_CAT_SPIRIT);
    CHECK(g_db.ingredients[I("angostura_bitters")].category == ADB_CAT_BITTERS);
    CHECK(g_db.ingredients[I("ginger_ale")].category == ADB_CAT_MIXER);
    CHECK(adb_guess_category("ginger_beer") == ADB_CAT_MIXER);
    CHECK(adb_guess_category("old_tom_gin") == ADB_CAT_SPIRIT);
    CHECK(adb_guess_category("coffee_liqueur") == ADB_CAT_LIQUEUR);
    CHECK_STR(g_db.ingredients[I("gin")].name, "Gin");
    /* category order: spirits come first */
    CHECK(g_db.ingredients[g_db.ing_by_category[0]].category == ADB_CAT_SPIRIT);
}

static void test_load_warnings(void)
{
    adb_db_t db;
    adb_db_init(&db);
    const char *j = "{\"recipes\":[{\"id\":\"x\",\"name\":\"X\",\"glass\":\"teacup\",\"method\":\"shaken\","
                    "\"ingredients\":[{\"ingredient_id\":\"gin\",\"display\":\"Gin\",\"amount\":50,\"unit\":\"ml\"}],"
                    "\"tags\":{\"flavor\":[\"umami\",\"sour\"]}},{\"name\":\"no id\"}]}";
    CHECK(adb_load_json_buffers(&db, j, NULL) == 0);
    CHECK(db.n_recipes == 1);
    CHECK(db.n_warnings == 3); /* teacup, umami, missing id */
    CHECK(db.recipes[0].glass == ADB_NONE);
    CHECK_STR(db.recipes[0].glass_key, "teacup");
    CHECK(db.recipes[0].flavor_mask == (1u << adb_tag_lookup(ADB_TAG_FLAVOR, "sour")));
    CHECK(adb_load_json_buffers(&db, "{not json", NULL) < 0);
    adb_db_free(&db);
}

static void test_filter_tags(void)
{
    load_sample(NULL);
    adb_filter_t f;
    adb_filter_clear(&f);
    CHECK(adb_filter_apply(&g_db, &f, NULL, 0) == 10);
    CHECK(adb_filter_active_count(&f) == 0);

    adb_filter_set_tag(&f, ADB_TAG_SPIRIT, adb_tag_lookup(ADB_TAG_SPIRIT, "gin"), true);
    CHECK_STR(match_ids(&f), "negroni");
    adb_filter_toggle_tag(&f, ADB_TAG_SPIRIT, adb_tag_lookup(ADB_TAG_SPIRIT, "vodka"));
    CHECK_STR(match_ids(&f), "espresso_martini,negroni");   /* OR within spirit */
    CHECK(adb_filter_active_count(&f) == 2);

    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_FLAVOR, adb_tag_lookup(ADB_TAG_FLAVOR, "sour"), true);
    CHECK_STR(match_ids(&f), "daiquiri,margarita,whiskey_sour");
    adb_filter_set_tag(&f, ADB_TAG_FLAVOR, adb_tag_lookup(ADB_TAG_FLAVOR, "fruity"), true);
    CHECK_STR(match_ids(&f), "daiquiri,margarita");          /* AND within flavor */

    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_ALCOHOLIC, ADB_ALC_MOCKTAIL, true);
    CHECK_STR(match_ids(&f), "shirley_temple");
    adb_filter_set_tag(&f, ADB_TAG_ALCOHOLIC, ADB_ALC_LOW_ABV, true);
    CHECK_STR(match_ids(&f), "mimosa,shirley_temple");

    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_STRENGTH, adb_tag_lookup(ADB_TAG_STRENGTH, "light"), true);
    CHECK_STR(match_ids(&f), "irish_coffee,mimosa,shirley_temple");

    adb_filter_clear(&f);   /* AND across kinds */
    adb_filter_set_tag(&f, ADB_TAG_SEASON, ADB_SEASON_SUMMER, true);
    adb_filter_set_tag(&f, ADB_TAG_METHOD, adb_tag_lookup(ADB_TAG_METHOD, "shaken"), true);
    CHECK_STR(match_ids(&f), "daiquiri,margarita");
    adb_filter_set_tag(&f, ADB_TAG_SPIRIT, adb_tag_lookup(ADB_TAG_SPIRIT, "tequila"), true);
    CHECK_STR(match_ids(&f), "margarita");

    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_GLASS, adb_tag_lookup(ADB_TAG_GLASS, "coupe"), true);
    CHECK_STR(match_ids(&f), "daiquiri,margarita");
    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_DIFFICULTY, 0, true);
    CHECK_STR(match_ids(&f), "mimosa,negroni,old_fashioned,shirley_temple");
    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_OCCASION, adb_tag_lookup(ADB_TAG_OCCASION, "brunch"), true);
    CHECK_STR(match_ids(&f), "mimosa,shirley_temple");
    adb_filter_clear(&f);
    adb_filter_set_tag(&f, ADB_TAG_SEASON, ADB_SEASON_HOLIDAY, true);
    CHECK_STR(match_ids(&f), "irish_coffee,mimosa,shirley_temple");
}

static void test_filter_ingredients(void)
{
    load_sample(NULL);
    adb_filter_t f;
    adb_filter_clear(&f);
    adb_filter_set_ing(&f, I("lime_juice"), 1);
    CHECK_STR(match_ids(&f), "daiquiri,margarita,shirley_temple"); /* optional counts */
    CHECK(adb_filter_ing_state(&f, I("lime_juice")) == 1);
    adb_filter_set_ing(&f, I("simple_syrup"), 1);
    CHECK_STR(match_ids(&f), "daiquiri");

    adb_filter_clear(&f);
    adb_filter_set_ing(&f, I("lime_juice"), -1);
    CHECK(adb_filter_apply(&g_db, &f, NULL, 0) == 7);
    CHECK(adb_filter_ing_state(&f, I("lime_juice")) == -1);
    adb_filter_set_ing(&f, I("lime_juice"), 1);  /* switching state moves it */
    CHECK(f.n_exclude == 0 && f.n_include == 1);
    adb_filter_set_ing(&f, I("lime_juice"), 0);
    CHECK(adb_filter_active_count(&f) == 0);

    /* include + exclude together */
    adb_filter_set_ing(&f, I("simple_syrup"), 1);
    adb_filter_set_ing(&f, I("vodka"), -1);
    CHECK_STR(match_ids(&f), "daiquiri,whiskey_sour");
}

static void test_filter_text_and_restrict(void)
{
    load_sample(NULL);
    adb_filter_t f;
    adb_filter_clear(&f);
    adb_filter_set_text(&f, "pina");
    CHECK_STR(match_ids(&f), "pina_colada");                /* accent folding */
    adb_filter_set_text(&f, "PIÑA");
    CHECK_STR(match_ids(&f), "pina_colada");
    adb_filter_set_text(&f, "campari");
    CHECK_STR(match_ids(&f), "negroni");                    /* ingredient text */
    adb_filter_set_text(&f, "coffee");
    CHECK_STR(match_ids(&f), "espresso_martini,irish_coffee");
    adb_filter_set_text(&f, "zzz");
    CHECK(adb_filter_apply(&g_db, &f, NULL, 0) == 0);
    CHECK(adb_filter_active_count(&f) == 1);

    adb_filter_clear(&f);
    uint8_t set[ADB_BITSET_BYTES(ADB_MAX_RECIPES)] = {0};
    adb_bit_set(set, (unsigned)R("mimosa"), true);
    adb_bit_set(set, (unsigned)R("negroni"), true);
    f.restrict_set = set;
    CHECK_STR(match_ids(&f), "mimosa,negroni");
    adb_filter_set_tag(&f, ADB_TAG_SPIRIT, adb_tag_lookup(ADB_TAG_SPIRIT, "gin"), true);
    CHECK_STR(match_ids(&f), "negroni");
    CHECK(adb_filter_active_count(&f) == 1);
}

static void pantry_add(uint8_t *p, const char *const *ids)
{
    for (; *ids; ids++) {
        uint16_t i = I(*ids);
        CHECK(i != ADB_ING_NONE);
        if (i != ADB_ING_NONE) adb_bit_set(p, i, true);
    }
}

static void test_pantry(void)
{
    load_sample(NULL);
    uint8_t p[ADB_BITSET_BYTES(ADB_MAX_INGREDIENTS)] = {0};
    adb_pantry_result_t res;
    adb_pantry_compute(&g_db, p, &res);
    CHECK(res.n[0] == 0 && res.n[1] == 0);
    CHECK(res.n[2] == 2);   /* mimosa (2 ings), shirley temple (2 required) */
    adb_pantry_result_free(&res);

    pantry_add(p, (const char *const[]){"gin", "campari", "sweet_vermouth", NULL});
    CHECK(adb_pantry_missing(&g_db, p, R("negroni"), NULL, 0) == 0);

    /* optional ingredients ignored: lime juice is optional in Shirley Temple */
    pantry_add(p, (const char *const[]){"ginger_ale", "grenadine", NULL});
    CHECK(adb_pantry_missing(&g_db, p, R("shirley_temple"), NULL, 0) == 0);

    /* water ignored: Old Fashioned needs whiskey, sugar, bitters */
    pantry_add(p, (const char *const[]){"bourbon_whiskey", "sugar_cube", "angostura_bitters", NULL});
    CHECK(adb_pantry_missing(&g_db, p, R("old_fashioned"), NULL, 0) == 0);

    /* missing 1 / 2 with the list of what's missing */
    pantry_add(p, (const char *const[]){"rum_white", "lime_juice", NULL});
    uint16_t miss[4];
    CHECK(adb_pantry_missing(&g_db, p, R("daiquiri"), miss, 4) == 1);
    CHECK(miss[0] == I("simple_syrup"));
    CHECK(adb_pantry_missing(&g_db, p, R("whiskey_sour"), miss, 4) == 2); /* lemon, syrup; egg/bitters optional */
    CHECK(adb_pantry_missing(&g_db, p, R("margarita"), NULL, 0) == 2);    /* salt optional */

    adb_pantry_compute(&g_db, p, &res);
    CHECK(res.n[0] == 3);                 /* negroni, old fashioned, shirley temple */
    CHECK(res.idx[0][0] == R("negroni")); /* sorted by name */
    CHECK(res.n[1] == 1 && res.idx[1][0] == R("daiquiri"));
    CHECK(res.n[2] == 4);                 /* margarita, mimosa, piña colada, whiskey sour */
    adb_pantry_result_free(&res);
}

static void test_pantry_substitutes_and_garnish(void)
{
    const char *ing =
        "{\"ingredients\":["
        "{\"id\":\"lime_juice\",\"name\":\"Lime juice\",\"category\":\"juice\",\"substitutes\":[\"lemon_juice\"]},"
        "{\"id\":\"lemon_juice\",\"name\":\"Lemon juice\",\"category\":\"juice\"},"
        "{\"id\":\"simple_syrup\",\"name\":\"Simple syrup\",\"category\":\"syrup\",\"pantry_essential\":true},"
        "{\"id\":\"salt\",\"name\":\"Salt\",\"category\":\"garnish\"},"
        "{\"id\":\"sparkling_wine\",\"name\":\"Sparkling wine\",\"category\":\"wine\",\"substitutes\":[\"champagne\",\"prosecco\"]}"
        "]}";
    load_sample(ing);
    CHECK(g_db.n_warnings == 0);
    CHECK_STR(g_db.ingredients[I("lime_juice")].name, "Lime juice");
    CHECK(g_db.ingredients[I("lime_juice")].n_subs == 1);
    CHECK(g_db.ingredients[I("simple_syrup")].pantry_essential);
    CHECK(g_db.ingredients[I("salt")].category == ADB_CAT_GARNISH);

    uint8_t p[ADB_BITSET_BYTES(ADB_MAX_INGREDIENTS)] = {0};
    pantry_add(p, (const char *const[]){"rum_white", "lemon_juice", "simple_syrup", NULL});
    CHECK(adb_pantry_has(&g_db, p, I("lime_juice")));         /* via substitute */
    CHECK(!adb_pantry_has(&g_db, p, I("triple_sec")));
    CHECK(adb_pantry_missing(&g_db, p, R("daiquiri"), NULL, 0) == 0);
    /* substitutes are one-directional as listed */
    memset(p, 0, sizeof p);
    pantry_add(p, (const char *const[]){"lime_juice", NULL});
    CHECK(!adb_pantry_has(&g_db, p, I("lemon_juice")));
    /* Mimosa with prosecco instead of sparkling wine */
    pantry_add(p, (const char *const[]){"orange_juice", "prosecco", NULL});
    CHECK(adb_pantry_missing(&g_db, p, R("mimosa"), NULL, 0) == 0);

    /* garnish category is ignored even when not optional */
    adb_db_t db;
    adb_db_init(&db);
    const char *rj = "{\"recipes\":[{\"id\":\"g\",\"name\":\"G\",\"ingredients\":["
                     "{\"ingredient_id\":\"gin\",\"display\":\"Gin\"},"
                     "{\"ingredient_id\":\"salt\",\"display\":\"Salt rim\"},"
                     "{\"ingredient_id\":\"gin\",\"display\":\"More gin\"}]}]}";
    CHECK(adb_load_json_buffers(&db, rj, ing) == 0);
    uint8_t q[8] = {0};
    CHECK(adb_pantry_missing(&db, q, 0, NULL, 0) == 1);       /* gin once, salt ignored */
    adb_db_free(&db);
}

static void test_storage(void)
{
    load_sample(NULL);
    adb_storage_t io;
    adb_storage_mem_t mem;
    adb_storage_mem_init(&io, &mem);

    adb_state_t a, b;
    CHECK(adb_state_init(&a, &g_db));
    adb_bit_set(a.favorites, (unsigned)R("negroni"), true);
    adb_bit_set(a.favorites, (unsigned)R("mimosa"), true);
    adb_bit_set(a.pantry, I("gin"), true);
    a.settings.brightness = 33;
    a.settings.use_oz = false;
    a.settings.sleep_s = 300;
    adb_owner_set(&a.settings, "  Jamie ");
    a.settings.splash_on = false;
    a.settings.next_logo = 3;
    CHECK(adb_state_save_favorites(&a, &g_db, &io));
    CHECK(adb_state_save_pantry(&a, &g_db, &io));
    CHECK(adb_state_save_settings(&a, &io));

    CHECK(adb_state_init(&b, &g_db));
    adb_state_load(&b, &g_db, &io);
    CHECK(adb_bit_get(b.favorites, (unsigned)R("negroni")));
    CHECK(adb_bit_get(b.favorites, (unsigned)R("mimosa")));
    CHECK(!adb_bit_get(b.favorites, (unsigned)R("daiquiri")));
    CHECK(adb_bit_get(b.pantry, I("gin")));
    CHECK(b.settings.brightness == 33 && !b.settings.use_oz && b.settings.sleep_s == 300);
    CHECK(strcmp(b.settings.owner, "Jamie") == 0);
    CHECK(!b.settings.splash_on && b.settings.next_logo == 3);
    adb_state_free(&b);

    /* unknown ids from an older dataset are dropped */
    io.write(io.ctx, "favorites", "gone_drink\nnegroni\n", 19);
    CHECK(adb_state_init(&b, &g_db));
    adb_state_load(&b, &g_db, &io);
    int count = 0;
    for (int i = 0; i < g_db.n_recipes; i++) count += adb_bit_get(b.favorites, (unsigned)i);
    CHECK(count == 1);
    adb_state_free(&b);
    adb_state_free(&a);
    adb_storage_mem_free(&mem);

    /* file backend round trip */
    char dir[] = "/tmp/adb_test_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    adb_storage_file_t fb;
    CHECK(adb_storage_file_init(&io, &fb, dir));
    CHECK(adb_state_init(&a, &g_db));
    adb_bit_set(a.favorites, (unsigned)R("daiquiri"), true);
    CHECK(adb_state_save_favorites(&a, &g_db, &io));
    CHECK(adb_state_init(&b, &g_db));
    adb_state_load(&b, &g_db, &io);
    CHECK(adb_bit_get(b.favorites, (unsigned)R("daiquiri")));
    CHECK(b.settings.brightness == 60);  /* defaults when key missing */
    CHECK(b.settings.owner[0] == 0 && b.settings.splash_on && b.settings.next_logo == 0);
    adb_state_free(&a);
    adb_state_free(&b);
    char path[300];
    snprintf(path, sizeof path, "%s/favorites.dat", dir);
    unlink(path);
    rmdir(dir);
}

static void test_owner(void)
{
    adb_settings_t s;
    char buf[64];
    adb_settings_default(&s);
    CHECK(strcmp(adb_owner_name(&s), "Aaron") == 0 && adb_owner_is_default(&s));
    adb_owner_possessive(&s, buf, sizeof buf, false);
    CHECK(strcmp(buf, "Aaron's") == 0);
    adb_owner_possessive(&s, buf, sizeof buf, true);
    CHECK(strcmp(buf, "AARON'S") == 0);

    adb_owner_set(&s, "  aaron ");                /* any case, trimmed */
    CHECK(adb_owner_is_default(&s) && strcmp(s.owner, "aaron") == 0);
    adb_owner_set(&s, "Aaronson");
    CHECK(!adb_owner_is_default(&s));
    adb_owner_set(&s, "   ");                     /* blank = default */
    CHECK(s.owner[0] == 0 && strcmp(adb_owner_name(&s), "Aaron") == 0);

    adb_owner_set(&s, "James");
    adb_owner_possessive(&s, buf, sizeof buf, false);
    CHECK(strcmp(buf, "James'") == 0);
    adb_owner_set(&s, "Zo\xC3\xAB");               /* UTF-8 kept, only ASCII upper-cased */
    adb_owner_possessive(&s, buf, sizeof buf, true);
    CHECK(strcmp(buf, "ZO\xC3\xAB'S") == 0);

    /* cut to ADB_OWNER_MAX without splitting a UTF-8 character */
    adb_owner_set(&s, "abcdefghijklmnopqrstuvw\xC3\xA9xyz");   /* 23 ASCII + 2-byte char */
    CHECK(strlen(s.owner) == 23);
    adb_owner_set(&s, "abcdefghijklmnopqrstuvwxyz");
    CHECK(strlen(s.owner) == ADB_OWNER_MAX);
}

static void test_format(void)
{
    char b[32];
    adb_recipe_ing_t ri = { .amount = 45, .unit = ADB_UNIT_ML };
    adb_format_amount(&ri, false, b, sizeof b); CHECK_STR(b, "45 ml");
    adb_format_amount(&ri, true, b, sizeof b);  CHECK_STR(b, "1\xC2\xBD oz");
    ri.amount = 30; adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "1 oz");
    ri.amount = 20; adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "\xC2\xBE oz");
    ri.amount = 5;  adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "1 tsp");
    ri.amount = 120; adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "4 oz");
    ri = (adb_recipe_ing_t){ .amount = 3, .unit = ADB_UNIT_DASH };
    adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "3 dashes");
    ri = (adb_recipe_ing_t){ .amount = 1, .unit = ADB_UNIT_CUBE };
    adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "1 cube");
    ri = (adb_recipe_ing_t){ .amount = -1, .unit = ADB_UNIT_PINCH };
    adb_format_amount(&ri, true, b, sizeof b); CHECK_STR(b, "Pinch");
    ri = (adb_recipe_ing_t){ .amount = -1, .unit = ADB_UNIT_TOP };
    adb_format_amount(&ri, false, b, sizeof b); CHECK_STR(b, "Top up");

    CHECK(adb_text_contains("Piña Colada", "na co"));
    CHECK(adb_text_contains("Crème de cassis", "creme"));
    CHECK(!adb_text_contains("Negroni", "gin"));
    CHECK(adb_text_contains("anything", ""));
}

/* The real dataset, packed by tools/data/build.py pack (see CMakeLists.txt). */
static void test_pack(void)
{
    FILE *fp = fopen(PACK_BIN, "rb");
    CHECK(fp != NULL);
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    CHECK(fread(buf, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);

    CHECK(adb_pack_toc_size(buf, 16) > 16);
    CHECK(adb_pack_image_size(buf, (size_t)n) == (size_t)n);
    adb_pack_t p;
    char err[96] = "";
    CHECK(adb_pack_open(&p, buf, (size_t)n, true, err, sizeof err) == 0);
    adb_db_t db;
    adb_db_init(&db);
    CHECK(adb_load_pack(&db, &p) == 0);
    CHECK(db.n_recipes >= 200);
    CHECK(db.n_warnings == 0);
    int photos = 0, found = 0;
    for (int i = 0; i < db.n_recipes; i++) {
        const adb_recipe_t *r = &db.recipes[i];
        if (!r->image.is_photo) { CHECK(r->image.thumb == NULL && r->image.file == NULL); continue; }
        photos++;
        size_t sz = 0;
        const uint8_t *t = adb_pack_find(&p, r->image.thumb, &sz);
        const uint8_t *d = adb_pack_find(&p, r->image.file, NULL);
        if (t && d && sz > 4 && t[0] == 0xFF && t[1] == 0xD8 && d[0] == 0xFF && d[1] == 0xD8 && t[sz] == 0) found++;
    }
    CHECK(photos > 100);
    CHECK(found == photos);
    CHECK(adb_pack_find(&p, "nope.jpg", NULL) == NULL);
    adb_db_free(&db);

    buf[n / 2] ^= 0x55;   /* corrupt a byte: CRC must catch it */
    CHECK(adb_pack_open(&p, buf, (size_t)n, true, err, sizeof err) != 0);
    CHECK(strstr(err, "CRC") != NULL);
    buf[n / 2] ^= 0x55;
    CHECK(adb_pack_open(&p, buf, (size_t)n - 100, false, err, sizeof err) != 0);   /* truncated */
    memcpy(buf, "XXXX", 4);
    CHECK(adb_pack_open(&p, buf, (size_t)n, false, err, sizeof err) != 0);
    free(buf);
}

int main(void)
{
    adb_db_init(&g_db);
    RUN(test_load);
    RUN(test_load_warnings);
    RUN(test_filter_tags);
    RUN(test_filter_ingredients);
    RUN(test_filter_text_and_restrict);
    RUN(test_pantry);
    RUN(test_pantry_substitutes_and_garnish);
    RUN(test_storage);
    RUN(test_owner);
    RUN(test_format);
    RUN(test_pack);
    adb_db_free(&g_db);
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
