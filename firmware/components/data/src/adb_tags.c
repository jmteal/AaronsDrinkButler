/* Tag value tables. The order of each table is the bit/index order used in
 * adb_recipe_t, so only append new values (packed datasets depend on it). */
#include <string.h>
#include "adb_data.h"

typedef struct { const char *key, *label; } kv_t;

static const kv_t k_spirit[] = {
    {"gin", "Gin"}, {"vodka", "Vodka"}, {"rum_white", "White rum"}, {"rum_dark", "Dark rum"},
    {"tequila", "Tequila"}, {"mezcal", "Mezcal"}, {"whiskey_bourbon", "Bourbon"},
    {"whiskey_rye", "Rye"}, {"whiskey_scotch", "Scotch"}, {"whiskey_irish", "Irish whiskey"},
    {"brandy_cognac", "Brandy"}, {"pisco", "Pisco"}, {"cachaca", "Cachaça"},
    {"amaro_aperitif", "Amaro"}, {"wine_sparkling", "Sparkling wine"},
    {"liqueur", "Liqueur"}, {"none", "No spirit"},
};
static const kv_t k_flavor[] = {
    {"sweet", "Sweet"}, {"sour", "Sour"}, {"bitter", "Bitter"},
    {"spirit_forward", "Spirit-forward"}, {"fruity", "Fruity"}, {"herbal", "Herbal"},
    {"creamy", "Creamy"}, {"spicy", "Spicy"}, {"smoky", "Smoky"}, {"bubbly", "Bubbly"},
    {"coffee", "Coffee"},
};
static const kv_t k_strength[] = { {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"} };
static const kv_t k_season[] = {
    {"spring", "Spring"}, {"summer", "Summer"}, {"fall", "Fall"}, {"winter", "Winter"},
    {"holiday", "Holiday"}, {"all_year", "All year"},
};
static const kv_t k_occasion[] = {
    {"brunch", "Brunch"}, {"party", "Party"}, {"aperitif", "Aperitif"},
    {"after_dinner", "After dinner"}, {"nightcap", "Nightcap"}, {"date_night", "Date night"},
    {"classic", "Classic"},
};
static const kv_t k_glass[] = {
    {"rocks", "Rocks"}, {"double_rocks", "Double rocks"}, {"coupe", "Coupe"},
    {"martini", "Martini"}, {"highball", "Highball"}, {"collins", "Collins"},
    {"flute", "Flute"}, {"wine", "Wine glass"}, {"copper_mug", "Copper mug"},
    {"hurricane", "Hurricane"}, {"tiki", "Tiki mug"}, {"irish_coffee_mug", "Irish coffee mug"},
    {"nick_and_nora", "Nick & Nora"}, {"shot", "Shot"},
};
static const kv_t k_method[] = {
    {"shaken", "Shaken"}, {"stirred", "Stirred"}, {"built", "Built"}, {"blended", "Blended"},
    {"layered", "Layered"}, {"muddled_built", "Muddled"}, {"hot", "Hot"},
};
static const kv_t k_difficulty[] = { {"1", "Easy"}, {"2", "Medium"}, {"3", "Advanced"} };
static const kv_t k_alcoholic[] = {
    {"alcoholic", "Alcoholic"}, {"low_abv", "Low ABV"}, {"mocktail", "Mocktail"},
};
static const kv_t k_ice[] = {
    {"none", "No ice"}, {"cubes", "Ice cubes"}, {"large_cube", "Large cube"},
    {"crushed", "Crushed ice"}, {"blended", "Blended"},
};
static const kv_t k_unit[] = {
    {"ml", "ml"}, {"dash", "dash"}, {"barspoon", "barspoon"}, {"tsp", "tsp"},
    {"cube", "cube"}, {"piece", "piece"}, {"leaf", "leaf"}, {"top", "top up"},
    {"pinch", "pinch"},
};
static const kv_t k_cat[] = {
    {"spirit", "Spirits"}, {"liqueur", "Liqueurs"}, {"wine", "Wine & vermouth"},
    {"mixer", "Mixers"}, {"juice", "Juices"}, {"syrup", "Syrups & sugar"},
    {"bitters", "Bitters"}, {"dairy", "Dairy"}, {"egg", "Eggs"}, {"garnish", "Garnish"},
    {"other", "Other"},
};

#define T(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
static const struct { const kv_t *t; int n; } k_tables[ADB_TAG_ALL_COUNT] = {
    [ADB_TAG_SPIRIT] = T(k_spirit),     [ADB_TAG_FLAVOR] = T(k_flavor),
    [ADB_TAG_STRENGTH] = T(k_strength), [ADB_TAG_SEASON] = T(k_season),
    [ADB_TAG_OCCASION] = T(k_occasion), [ADB_TAG_GLASS] = T(k_glass),
    [ADB_TAG_METHOD] = T(k_method),     [ADB_TAG_DIFFICULTY] = T(k_difficulty),
    [ADB_TAG_ALCOHOLIC] = T(k_alcoholic), [ADB_TAG_ICE] = T(k_ice),
    [ADB_TAG_UNIT] = T(k_unit),         [ADB_TAG_ING_CATEGORY] = T(k_cat),
};

static const char *k_kind_labels[ADB_TAG_COUNT] = {
    "Spirit", "Flavor", "Strength", "Season", "Occasion", "Glass", "Method",
    "Difficulty", "Type",
};

int adb_tag_count(adb_tag_kind_t kind)
{
    return (kind < ADB_TAG_ALL_COUNT) ? k_tables[kind].n : 0;
}

const char *adb_tag_key(adb_tag_kind_t kind, int idx)
{
    if (kind >= ADB_TAG_ALL_COUNT || idx < 0 || idx >= k_tables[kind].n) return "";
    return k_tables[kind].t[idx].key;
}

const char *adb_tag_label(adb_tag_kind_t kind, int idx)
{
    if (kind >= ADB_TAG_ALL_COUNT || idx < 0 || idx >= k_tables[kind].n) return "?";
    return k_tables[kind].t[idx].label;
}

int adb_tag_lookup(adb_tag_kind_t kind, const char *key)
{
    if (!key || kind >= ADB_TAG_ALL_COUNT) return -1;
    for (int i = 0; i < k_tables[kind].n; i++)
        if (strcmp(k_tables[kind].t[i].key, key) == 0) return i;
    return -1;
}

const char *adb_tag_kind_label(adb_tag_kind_t kind)
{
    return kind < ADB_TAG_COUNT ? k_kind_labels[kind] : "";
}

bool adb_tag_is_multi(adb_tag_kind_t kind)
{
    return kind == ADB_TAG_SPIRIT || kind == ADB_TAG_FLAVOR || kind == ADB_TAG_SEASON ||
           kind == ADB_TAG_OCCASION;
}
