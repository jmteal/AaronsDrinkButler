/*
 * Generated glass icons: used for every drink without a photo.
 *
 * Each icon is drawn from the recipe's glass, liquid colour, ice and
 * garnish with a tiny anti-aliased polygon rasterizer straight into an
 * RGB565 lv_draw_buf_t, so it costs no flash per drink and looks the same
 * on the device and in the simulator. Shapes are defined in a 100x100
 * design space and scaled.
 *
 * Thumbnails are cached (LRU); the detail photo placeholder is cached once.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

/* Public LVGL 9 API, but its header is not pulled in by lvgl.h. */
void lv_image_cache_drop(const void *src);

typedef struct { float x, y; } pt_t;

typedef struct {
    lv_draw_buf_t *buf;
    float ox, oy, s;       /* design space -> pixels */
} canvas_t;

#define MAXP 96

/* ---- Rasterizer ------------------------------------------------------- */

static inline uint16_t blend565(uint16_t dst, uint32_t rgb, float a)
{
    if (a <= 0.0f) return dst;
    if (a > 1.0f) a = 1.0f;
    int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    int sr = (int)((rgb >> 19) & 0x1F), sg = (int)((rgb >> 10) & 0x3F), sb = (int)((rgb >> 3) & 0x1F);
    int r = (int)(dr + (sr - dr) * a + 0.5f), g = (int)(dg + (sg - dg) * a + 0.5f), b = (int)(db + (sb - db) * a + 0.5f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void clear(canvas_t *c, uint32_t rgb)
{
    uint32_t w = c->buf->header.w, h = c->buf->header.h, stride = c->buf->header.stride;
    uint16_t px = blend565(0, rgb, 1.0f);
    for (uint32_t y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)(c->buf->data + y * stride);
        for (uint32_t x = 0; x < w; x++) row[x] = px;
    }
}

/* Even-odd fill of a polygon given in design space. 4 sub-scanlines per
 * pixel, exact horizontal coverage. */
static void fill(canvas_t *c, const pt_t *in, int n, uint32_t rgb, float alpha)
{
    if (n < 3) return;
    pt_t p[MAXP];
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n && i < MAXP; i++) {
        p[i].x = c->ox + in[i].x * c->s;
        p[i].y = c->oy + in[i].y * c->s;
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    int W = (int)c->buf->header.w, H = (int)c->buf->header.h;
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1) return;
    float cover[UI_HOR_RES + 1];
    for (int y = y0; y < y1; y++) {
        memset(cover, 0, sizeof(float) * (size_t)(x1 - x0 + 1));
        bool any = false;
        for (int sub = 0; sub < 4; sub++) {
            float sy = (float)y + 0.125f + 0.25f * (float)sub;
            float xs[MAXP];
            int nx = 0;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                const pt_t *a = &p[i], *b = &p[j];
                if ((a->y <= sy && b->y > sy) || (b->y <= sy && a->y > sy))
                    xs[nx++] = a->x + (sy - a->y) * (b->x - a->x) / (b->y - a->y);
            }
            for (int i = 1; i < nx; i++) { /* insertion sort */
                float v = xs[i];
                int k = i - 1;
                while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; }
                xs[k + 1] = v;
            }
            for (int i = 0; i + 1 < nx; i += 2) {
                float xa = xs[i] < (float)x0 ? (float)x0 : xs[i];
                float xb = xs[i + 1] > (float)x1 ? (float)x1 : xs[i + 1];
                for (int px = (int)floorf(xa); px < x1 && (float)px < xb; px++) {
                    float lo = xa > (float)px ? xa : (float)px;
                    float hi = xb < (float)(px + 1) ? xb : (float)(px + 1);
                    if (hi > lo) { cover[px - x0] += (hi - lo) * 0.25f; any = true; }
                }
            }
        }
        if (!any) continue;
        uint16_t *row = (uint16_t *)(c->buf->data + (uint32_t)y * c->buf->header.stride);
        for (int x = x0; x < x1; x++)
            if (cover[x - x0] > 0.0f) row[x] = blend565(row[x], rgb, cover[x - x0] * alpha);
    }
}

/* ---- Shape helpers (design space) -------------------------------------- */

static int ellipse_pts(pt_t *out, float cx, float cy, float rx, float ry, float a0, float a1, int segs)
{
    for (int i = 0; i <= segs; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)segs;
        out[i].x = cx + rx * cosf(a);
        out[i].y = cy + ry * sinf(a);
    }
    return segs + 1;
}

static void ellipse(canvas_t *c, float cx, float cy, float rx, float ry, uint32_t rgb, float alpha)
{
    pt_t p[33];
    int n = ellipse_pts(p, cx, cy, rx, ry, 0, 6.2831853f, 32);
    fill(c, p, n - 1, rgb, alpha);
}

static void rot_ellipse(canvas_t *c, float cx, float cy, float rx, float ry, float rot, uint32_t rgb)
{
    pt_t p[24];
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < 24; i++) {
        float a = 6.2831853f * (float)i / 24.0f, x = rx * cosf(a), y = ry * sinf(a);
        p[i].x = cx + x * cr - y * sr;
        p[i].y = cy + x * sr + y * cr;
    }
    fill(c, p, 24, rgb, 1.0f);
}

static void rect(canvas_t *c, float x0, float y0, float x1, float y1, uint32_t rgb, float alpha)
{
    pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    fill(c, p, 4, rgb, alpha);
}

static void line(canvas_t *c, float xa, float ya, float xb, float yb, float w, uint32_t rgb, float alpha)
{
    float dx = xb - xa, dy = yb - ya, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    pt_t p[4] = {{xa + nx, ya + ny}, {xb + nx, yb + ny}, {xb - nx, yb - ny}, {xa - nx, ya - ny}};
    fill(c, p, 4, rgb, alpha);
}

/* Keep the part of a polygon with y >= ylim (Sutherland-Hodgman, one plane). */
static int clip_below(const pt_t *in, int n, float ylim, pt_t *out)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        pt_t a = in[i], b = in[(i + 1) % n];
        bool ain = a.y >= ylim, bin = b.y >= ylim;
        if (ain) out[m++] = a;
        if (ain != bin) {
            float t = (ylim - a.y) / (b.y - a.y);
            out[m++] = (pt_t){a.x + t * (b.x - a.x), ylim};
        }
    }
    return m;
}

/* Inset a convex polygon by d (edge offsetting). */
static int inset(const pt_t *in, int n, float d, pt_t *out)
{
    float area = 0;
    for (int i = 0; i < n; i++) {
        const pt_t *a = &in[i], *b = &in[(i + 1) % n];
        area += a->x * b->y - b->x * a->y;
    }
    float sgn = area > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < n; i++) {
        const pt_t *p0 = &in[(i + n - 1) % n], *p1 = &in[i], *p2 = &in[(i + 1) % n];
        float e1x = p1->x - p0->x, e1y = p1->y - p0->y, e2x = p2->x - p1->x, e2y = p2->y - p1->y;
        float l1 = sqrtf(e1x * e1x + e1y * e1y), l2 = sqrtf(e2x * e2x + e2y * e2y);
        if (l1 < 1e-4f || l2 < 1e-4f) { out[i] = *p1; continue; }
        /* inward normals */
        float n1x = -e1y / l1 * sgn, n1y = e1x / l1 * sgn, n2x = -e2y / l2 * sgn, n2y = e2x / l2 * sgn;
        float bx = n1x + n2x, by = n1y + n2y, bl = sqrtf(bx * bx + by * by);
        if (bl < 1e-4f) { out[i] = (pt_t){p1->x + n1x * d, p1->y + n1y * d}; continue; }
        bx /= bl; by /= bl;
        float cosh = bx * n1x + by * n1y;
        float k = d / (cosh > 0.2f ? cosh : 0.2f);
        out[i] = (pt_t){p1->x + bx * k, p1->y + by * k};
    }
    return n;
}

/* ---- Glass drawing ------------------------------------------------------- */

#define GLASS_RIM   0xDCE6EA
#define GLASS_TINT  0x8FA3AD

typedef struct {
    pt_t bowl[MAXP];
    int  n;
    float fill_y;            /* liquid level */
    float rim_x, rim_y;      /* garnish anchor (top right of the bowl) */
    bool  stem;
    float stem_y0, stem_y1, base_rx;
    bool  opaque;            /* mug/tiki: body is not glass */
    uint32_t body_rgb;
    bool  ice_ok;
} glass_t;

static void poly(glass_t *g, const pt_t *p, int n)
{
    memcpy(g->bowl, p, sizeof(pt_t) * (size_t)n);
    g->n = n;
}

static void shape_for(const char *key, glass_t *g)
{
    memset(g, 0, sizeof(*g));
    g->ice_ok = true;
    int glass = adb_tag_lookup(ADB_TAG_GLASS, key);
    const char *k = glass >= 0 ? key : "rocks";
    if (!strcmp(k, "rocks")) {
        pt_t p[] = {{22, 38}, {78, 38}, {75, 86}, {25, 86}};
        poly(g, p, 4); g->fill_y = 54; g->rim_x = 74; g->rim_y = 38;
    } else if (!strcmp(k, "double_rocks")) {
        pt_t p[] = {{18, 32}, {82, 32}, {78, 86}, {22, 86}};
        poly(g, p, 4); g->fill_y = 48; g->rim_x = 78; g->rim_y = 32;
    } else if (!strcmp(k, "highball")) {
        pt_t p[] = {{30, 18}, {70, 18}, {68, 88}, {32, 88}};
        poly(g, p, 4); g->fill_y = 32; g->rim_x = 66; g->rim_y = 18;
    } else if (!strcmp(k, "collins")) {
        pt_t p[] = {{33, 12}, {67, 12}, {66, 88}, {34, 88}};
        poly(g, p, 4); g->fill_y = 26; g->rim_x = 64; g->rim_y = 12;
    } else if (!strcmp(k, "shot")) {
        pt_t p[] = {{36, 50}, {64, 50}, {61, 86}, {39, 86}};
        poly(g, p, 4); g->fill_y = 58; g->rim_x = 62; g->rim_y = 50; g->ice_ok = false;
    } else if (!strcmp(k, "coupe")) {
        g->n = ellipse_pts(g->bowl, 50, 32, 34, 22, 0, 3.14159265f, 20);
        g->fill_y = 36; g->rim_x = 80; g->rim_y = 32; g->stem = true;
        g->stem_y0 = 53; g->stem_y1 = 84; g->base_rx = 18; g->ice_ok = false;
    } else if (!strcmp(k, "nick_and_nora")) {
        g->n = ellipse_pts(g->bowl, 50, 28, 24, 26, 0, 3.14159265f, 20);
        g->fill_y = 33; g->rim_x = 70; g->rim_y = 28; g->stem = true;
        g->stem_y0 = 53; g->stem_y1 = 84; g->base_rx = 16; g->ice_ok = false;
    } else if (!strcmp(k, "martini")) {
        pt_t p[] = {{14, 22}, {86, 22}, {52, 58}, {48, 58}};
        poly(g, p, 4); g->fill_y = 29; g->rim_x = 80; g->rim_y = 22; g->stem = true;
        g->stem_y0 = 57; g->stem_y1 = 84; g->base_rx = 18; g->ice_ok = false;
    } else if (!strcmp(k, "flute")) {
        pt_t p[] = {{38, 10}, {62, 10}, {61, 46}, {56, 58}, {44, 58}, {39, 46}};
        poly(g, p, 6); g->fill_y = 20; g->rim_x = 60; g->rim_y = 10; g->stem = true;
        g->stem_y0 = 57; g->stem_y1 = 86; g->base_rx = 15; g->ice_ok = false;
    } else if (!strcmp(k, "wine")) {
        pt_t e[40];
        int n = ellipse_pts(e, 50, 34, 25, 27, -3.14159265f, 3.14159265f, 36);
        g->n = clip_below(e, n - 1, 12, g->bowl);
        g->fill_y = 34; g->rim_x = 70; g->rim_y = 14; g->stem = true;
        g->stem_y0 = 60; g->stem_y1 = 86; g->base_rx = 17; g->ice_ok = false;
    } else if (!strcmp(k, "hurricane")) {
        pt_t e[40];
        int n = ellipse_pts(e, 50, 46, 22, 36, -3.14159265f, 3.14159265f, 36);
        g->n = clip_below(e, n - 1, 12, g->bowl);
        g->fill_y = 22; g->rim_x = 66; g->rim_y = 12; g->stem = true;
        g->stem_y0 = 81; g->stem_y1 = 88; g->base_rx = 15;
    } else if (!strcmp(k, "copper_mug")) {
        pt_t p[] = {{24, 26}, {72, 26}, {70, 84}, {26, 84}};
        poly(g, p, 4); g->fill_y = 26; g->rim_x = 68; g->rim_y = 26;
        g->opaque = true; g->body_rgb = 0xB86B3A;
    } else if (!strcmp(k, "tiki")) {
        pt_t p[] = {{30, 16}, {70, 16}, {68, 88}, {32, 88}};
        poly(g, p, 4); g->fill_y = 16; g->rim_x = 66; g->rim_y = 16;
        g->opaque = true; g->body_rgb = 0x8A5A32;
    } else { /* irish_coffee_mug */
        pt_t p[] = {{28, 20}, {72, 20}, {68, 64}, {32, 64}};
        poly(g, p, 4); g->fill_y = 28; g->rim_x = 70; g->rim_y = 20; g->stem = true;
        g->stem_y0 = 63; g->stem_y1 = 82; g->base_rx = 16; g->ice_ok = false;
    }
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    int r = (int)(((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t);
    int g = (int)(((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t);
    int bl = (int)((a & 0xFF) * (1 - t) + (b & 0xFF) * t);
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

static bool has(const char *s, const char *needle) { return s && strstr(s, needle); }

static void draw_garnish(canvas_t *c, const char *g, float x, float y)
{
    if (!g) return;
    if (has(g, "cherry")) {
        line(c, x, y + 2, x + 6, y - 12, 1.6f, 0x3E6B2A, 1);
        ellipse(c, x, y + 4, 6, 6, 0xB71C1C, 1);
        ellipse(c, x - 2, y + 2, 1.8f, 1.8f, 0xFF8A80, 1);
    } else if (has(g, "olive")) {
        line(c, x - 14, y - 10, x + 8, y + 12, 1.6f, 0xC8A165, 1);
        ellipse(c, x - 3, y + 1, 6, 4.5f, 0x7C9A3A, 1);
        ellipse(c, x - 1, y + 1, 1.8f, 1.8f, 0xC62828, 1);
    } else if (has(g, "twist") || has(g, "peel")) {
        uint32_t col = has(g, "lemon") ? 0xF2C744 : (has(g, "lime") ? 0x8BC34A : 0xF08A24);
        for (int i = 0; i < 6; i++) { /* spiral */
            float a0 = (float)i * 1.1f, a1 = (float)(i + 1) * 1.1f, r0 = 3 + (float)i * 1.4f, r1 = 3 + (float)(i + 1) * 1.4f;
            line(c, x + r0 * cosf(a0), y - 4 + r0 * sinf(a0) * 0.6f, x + r1 * cosf(a1), y - 4 + r1 * sinf(a1) * 0.6f, 3.2f, col, 1);
        }
    } else if (has(g, "wheel") || has(g, "slice") || has(g, "wedge")) {
        uint32_t outer = 0xF08A24, inner = 0xFFC46B;
        if (has(g, "lime")) { outer = 0x6FA53A; inner = 0xC5E1A5; }
        if (has(g, "lemon")) { outer = 0xE6B800; inner = 0xFFF0A0; }
        ellipse(c, x, y, 11, 11, outer, 1);
        ellipse(c, x, y, 8.5f, 8.5f, inner, 1);
        for (int i = 0; i < 6; i++) {
            float a = (float)i * 1.0472f;
            line(c, x, y, x + 8 * cosf(a), y + 8 * sinf(a), 1.0f, outer, 0.7f);
        }
    } else if (has(g, "mint")) {
        rot_ellipse(c, x - 4, y - 6, 7, 3.5f, -0.9f, 0x3F8F3A);
        rot_ellipse(c, x + 3, y - 8, 7, 3.5f, -2.2f, 0x4CAF50);
        rot_ellipse(c, x, y - 2, 6, 3, -1.57f, 0x2E7D32);
    } else if (has(g, "pineapple")) {
        pt_t w[] = {{x - 10, y + 2}, {x + 10, y + 2}, {x, y - 12}};
        fill(c, w, 3, 0xF4C430, 1);
        rot_ellipse(c, x - 3, y - 16, 6, 2, -1.2f, 0x4C8C2B);
        rot_ellipse(c, x + 3, y - 16, 6, 2, -1.9f, 0x4C8C2B);
    } else if (has(g, "umbrella")) {
        line(c, x, y + 8, x - 6, y - 14, 1.4f, 0xC8A165, 1);
        pt_t u[16];
        int n = ellipse_pts(u, x - 6, y - 14, 12, 7, 3.14159265f, 6.2831853f, 14);
        fill(c, u, n, 0xEC407A, 1);
    } else if (has(g, "coffee")) {
        rot_ellipse(c, 44, y + 5, 3.2f, 2.2f, 0.3f, 0x3B2416);
        rot_ellipse(c, 50, y + 4, 3.2f, 2.2f, -0.2f, 0x3B2416);
        rot_ellipse(c, 56, y + 5, 3.2f, 2.2f, 0.5f, 0x3B2416);
    }
}

static void render(canvas_t *c, const adb_recipe_t *r, uint32_t bg)
{
    glass_t g;
    shape_for(r->glass_key, &g);
    uint32_t liquid = r->color_rgb ? r->color_rgb : 0xC08040;
    pt_t tmp[MAXP], liq[MAXP];

    /* stem + base */
    if (g.stem) {
        line(c, 50, g.stem_y0, 50, g.stem_y1, 3.0f, GLASS_RIM, 0.9f);
        ellipse(c, 50, g.stem_y1 + 1, g.base_rx, 3.2f, GLASS_RIM, 0.9f);
    }
    /* handles */
    if (has(r->glass_key, "copper_mug")) {
        line(c, 71, 38, 84, 42, 4, 0x9C552C, 1); line(c, 84, 42, 84, 66, 4, 0x9C552C, 1);
        line(c, 84, 66, 70, 70, 4, 0x9C552C, 1);
    } else if (has(r->glass_key, "irish_coffee")) {
        line(c, 70, 28, 82, 32, 3, GLASS_RIM, 0.9f); line(c, 82, 32, 82, 50, 3, GLASS_RIM, 0.9f);
        line(c, 82, 50, 67, 56, 3, GLASS_RIM, 0.9f);
    }

    if (g.opaque) {
        fill(c, g.bowl, g.n, g.body_rgb, 1);
        /* liquid surface */
        float x0 = g.bowl[0].x + 3, x1 = g.bowl[1].x - 3;
        ellipse(c, (x0 + x1) / 2, g.bowl[0].y + 3, (x1 - x0) / 2, 3.5f, liquid, 1);
        if (has(r->glass_key, "tiki")) { /* face */
            rect(c, 38, 34, 46, 40, 0x4A2C14, 1); rect(c, 54, 34, 62, 40, 0x4A2C14, 1);
            rect(c, 46, 46, 54, 58, 0x6B4220, 1); rect(c, 38, 66, 62, 72, 0x4A2C14, 1);
        } else { /* hammered copper highlights */
            rect(c, 30, 34, 34, 76, 0xE0A070, 0.6f);
        }
    } else {
        /* glass body: bright rim, faint tinted inside */
        fill(c, g.bowl, g.n, GLASS_RIM, 0.95f);
        int ni = inset(g.bowl, g.n, 2.2f, tmp);
        fill(c, tmp, ni, mix(bg, GLASS_TINT, 0.35f), 1);
        int nl = clip_below(tmp, ni, g.fill_y, liq);
        fill(c, liq, nl, liquid, 1);
        if (has(r->garnish_icon, "cream") || has(r->color_name, "layered")) { /* cream float / top layer */
            int nc = clip_below(tmp, ni, g.fill_y, liq);
            pt_t band[MAXP];
            float ytop = g.fill_y + 9;
            /* keep the part between fill_y and ytop: clip_below then invert by clipping above */
            int m = 0;
            for (int i = 0; i < nc; i++) {
                pt_t a = liq[i], b = liq[(i + 1) % nc];
                bool ain = a.y <= ytop, bin = b.y <= ytop;
                if (ain) band[m++] = a;
                if (ain != bin) { float t = (ytop - a.y) / (b.y - a.y); band[m++] = (pt_t){a.x + t * (b.x - a.x), ytop}; }
            }
            fill(c, band, m, 0xF1E3C6, 1);
        }
        float bx0 = 1e9f, bx1 = -1e9f;
        for (int i = 0; i < g.n; i++) {
            if (g.bowl[i].x < bx0) bx0 = g.bowl[i].x;
            if (g.bowl[i].x > bx1) bx1 = g.bowl[i].x;
        }
        /* ice */
        if (g.ice_ok && r->ice != ADB_NONE && r->ice != 0 /* none */) {
            float cx = (bx0 + bx1) / 2;
            if (r->ice == 2 /* large_cube */) {
                rect(c, cx - 13, g.fill_y + 4, cx + 13, g.fill_y + 30, 0xFFFFFF, 0.35f);
            } else if (r->ice == 4 /* blended */) {
                for (int i = 0; i < 9; i++)
                    ellipse(c, cx - 12 + (float)(i % 3) * 12, g.fill_y + 8 + (float)(i / 3) * 10, 2.2f, 2.2f, 0xFFFFFF, 0.35f);
            } else {
                rect(c, cx - 14, g.fill_y + 3, cx - 2, g.fill_y + 15, 0xFFFFFF, 0.35f);
                rect(c, cx + 1, g.fill_y + 8, cx + 13, g.fill_y + 20, 0xFFFFFF, 0.30f);
                rect(c, cx - 9, g.fill_y + 18, cx + 3, g.fill_y + 30, 0xFFFFFF, 0.25f);
            }
        }
        /* highlight */
        float hx = bx0 + 6;
        line(c, hx, g.rim_y + 6, hx, g.rim_y + 6 + (g.fill_y - g.rim_y) * 0.9f + 8, 2.4f, 0xFFFFFF, 0.35f);
    }
    if (has(r->garnish_icon, "salt") || has(r->garnish, "salt rim")) {
        float x0 = g.bowl[0].x, x1 = g.bowl[1].x;
        line(c, x0, g.rim_y, x1, g.rim_y, 3, 0xF5F5F5, 1);
    }
    draw_garnish(c, r->garnish_icon, g.rim_x, g.rim_y);
}

static lv_draw_buf_t *make_icon(uint16_t recipe, uint32_t w, uint32_t h, uint32_t bg)
{
    lv_draw_buf_t *buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
    if (!buf) return NULL;
    canvas_t c = { .buf = buf };
    float box = (float)(w < h ? w : h) * (w == h ? 1.0f : 0.84f);
    c.s = box / 100.0f;
    c.ox = ((float)w - box) / 2;
    c.oy = ((float)h - box) / 2 + (w == h ? 0.0f : 6.0f);
    clear(&c, bg);
    if (w != h) { /* photo placeholder: soft vignette disc in the liquid colour */
        const adb_recipe_t *r = &g_ui.db->recipes[recipe];
        canvas_t full = { .buf = buf, .s = (float)w / 100.0f, .ox = 0, .oy = ((float)h - (float)w) / 2 };
        ellipse(&full, 50, 50, 34, 34, mix(bg, r->color_rgb ? r->color_rgb : 0x806040, 0.22f), 1);
    }
    render(&c, &g_ui.db->recipes[recipe], bg);
    return buf;
}

/* ---- Caches ----------------------------------------------------------- */

#define THUMB_CACHE 48
static struct { int32_t recipe; uint32_t stamp; lv_draw_buf_t *buf; } s_thumbs[THUMB_CACHE];
static uint32_t s_stamp;
static int32_t s_photo_recipe = -1;
static lv_draw_buf_t *s_photo;

static void drop(lv_draw_buf_t *b)
{
    if (!b) return;
    lv_image_cache_drop(b);
    lv_draw_buf_destroy(b);
}

const void *ui_recipe_thumb(uint16_t recipe)
{
    if (g_ui.img && g_ui.img->thumb) {
        const void *src = g_ui.img->thumb(g_ui.img->ctx, g_ui.db, recipe);
        if (src) return src;
    }
    int victim = 0;
    for (int i = 0; i < THUMB_CACHE; i++) {
        if (s_thumbs[i].buf && s_thumbs[i].recipe == recipe) {
            s_thumbs[i].stamp = ++s_stamp;
            return s_thumbs[i].buf;
        }
        if (!s_thumbs[i].buf || s_thumbs[i].stamp < s_thumbs[victim].stamp) victim = i;
        if (!s_thumbs[victim].buf) break;
    }
    drop(s_thumbs[victim].buf);
    s_thumbs[victim].buf = make_icon(recipe, UI_THUMB, UI_THUMB, UI_C_SURFACE2);
    s_thumbs[victim].recipe = recipe;
    s_thumbs[victim].stamp = ++s_stamp;
    return s_thumbs[victim].buf;
}

const void *ui_recipe_photo(uint16_t recipe)
{
    if (g_ui.img && g_ui.img->photo) {
        const void *src = g_ui.img->photo(g_ui.img->ctx, g_ui.db, recipe);
        if (src) return src;
    }
    if (s_photo && s_photo_recipe == recipe) return s_photo;
    drop(s_photo);
    s_photo = make_icon(recipe, UI_PHOTO_W, UI_PHOTO_H, UI_C_SURFACE);
    s_photo_recipe = recipe;
    return s_photo;
}

void ui_icon_cache_clear(void)
{
    for (int i = 0; i < THUMB_CACHE; i++) {
        drop(s_thumbs[i].buf);
        s_thumbs[i].buf = NULL;
    }
    drop(s_photo);
    s_photo = NULL;
    s_photo_recipe = -1;
}
