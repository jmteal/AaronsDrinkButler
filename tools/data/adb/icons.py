"""Generated glass + liquid + garnish icons (our own artwork, MIT/CC BY 4.0 with the repo).

Every drink gets a 96x96 thumbnail and a 320x240 detail icon. They are drawn at 4x and
downsampled, then saved as palette PNGs so they stay small in flash.

Geometry lives in a 100x100 unit box: x=50 is the glass centre line, y grows downward.
"""

from __future__ import annotations

import math
from functools import lru_cache

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SS = 4  # supersampling factor

BG_TOP = (44, 33, 27)
BG_BOTTOM = (22, 17, 14)
GLASS_FILL = (255, 255, 255, 34)
GLASS_EDGE = (236, 228, 214, 215)
HIGHLIGHT = (255, 255, 255, 70)

# Bowl profiles: (y, half-width) from the rim down to the bottom of the bowl.
# stem: (y_top, y_bottom) of a stem; foot: half-width of the foot; base: glass slab below bowl.
GLASSES = {
    "rocks":        dict(bowl=[(36, 25), (84, 22)], base=6, fill=52),
    "double_rocks": dict(bowl=[(28, 27), (86, 24)], base=5, fill=40),
    "highball":     dict(bowl=[(12, 18), (88, 16)], base=5, fill=26),
    "collins":      dict(bowl=[(6, 14), (90, 13)], base=5, fill=20),
    "shot":         dict(bowl=[(52, 14), (86, 11)], base=6, fill=58),
    "coupe":        dict(bowl="coupe", stem=(52, 84), foot=19, fill=35),
    "martini":      dict(bowl=[(22, 33), (56, 1.6)], stem=(56, 86), foot=19, fill=28),
    "nick_and_nora": dict(bowl=[(24, 19), (34, 21), (44, 19), (52, 12), (57, 3), (58, 1.6)], stem=(58, 86), foot=17, fill=31),
    "flute":        dict(bowl=[(6, 11), (18, 12.5), (44, 10.5), (54, 6), (58, 1.6)], stem=(58, 88), foot=15, fill=14),
    "wine":         dict(bowl=[(12, 19), (24, 24), (38, 24.5), (50, 18), (57, 8), (59, 1.6)], stem=(59, 88), foot=18, fill=31),
    "hurricane":    dict(bowl=[(8, 19), (20, 18), (36, 11), (50, 13), (63, 17), (72, 13), (77, 5), (78, 2)], stem=(78, 86), foot=17, fill=18),
    "irish_coffee_mug": dict(bowl=[(14, 21), (64, 19), (69, 12), (71, 3)], stem=(71, 84), foot=16, fill=20, handle=True),
    "copper_mug":   dict(opaque="copper", fill=26),
    "tiki":         dict(opaque="tiki", fill=18),
}

FRUIT = {
    "lime": ((140, 198, 63), (78, 138, 46)),
    "lemon": ((246, 214, 72), (214, 170, 30)),
    "orange": ((246, 154, 51), (214, 108, 24)),
    "cucumber": ((205, 230, 170), (74, 128, 52)),
    "apple": ((236, 236, 190), (126, 186, 60)),
    "ginger": ((236, 210, 150), (190, 150, 90)),
}
# Colours used when a drink has a float or a sink layer.
LAYER_COLORS = {
    "grenadine": (176, 18, 42), "creme_de_mure": (92, 22, 60), "creme_de_cassis": (98, 20, 52),
    "rum_dark": (86, 42, 20), "heavy_cream": (246, 240, 226), "red_wine": (120, 22, 40),
    "galliano": (240, 205, 70), "espresso": (60, 36, 22), "islay_scotch": (214, 160, 70),
    "coffee_liqueur": (52, 28, 16), "irish_cream": (206, 170, 128), "grand_marnier": (214, 140, 48),
}


def hex_rgb(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def shade(c, f: float):
    if f >= 1:
        return tuple(min(255, int(v + (255 - v) * (f - 1))) for v in c[:3])
    return tuple(int(v * f) for v in c[:3])


# --------------------------------------------------------------------------- geometry


def bowl_points(spec) -> list[tuple[float, float]]:
    if spec == "coupe":
        pts = []
        for k in range(0, 21):
            t = k / 20
            y = 31 + 21 * t
            pts.append((y, 31 * math.sqrt(max(0.0, 1 - t ** 1.6)) + 1.6 * t))
        return pts
    pts = []
    for (y0, w0), (y1, w1) in zip(spec, spec[1:]):
        for k in range(8):
            t = k / 8
            pts.append((y0 + (y1 - y0) * t, w0 + (w1 - w0) * t))
    pts.append(spec[-1])
    return pts


def width_at(pts, y):
    if y <= pts[0][0]:
        return pts[0][1]
    for (y0, w0), (y1, w1) in zip(pts, pts[1:]):
        if y0 <= y <= y1:
            return w0 + (w1 - w0) * (y - y0) / max(1e-6, y1 - y0)
    return pts[-1][1]


def polygon(pts, y_from=None):
    """Closed polygon of the bowl interior, optionally only below y_from."""
    if y_from is not None:
        inner = [(y_from, width_at(pts, y_from))] + [p for p in pts if p[0] > y_from]
    else:
        inner = pts
    right = [(50 + w, y) for y, w in inner]
    left = [(50 - w, y) for y, w in reversed(inner)]
    return right + left


# --------------------------------------------------------------------------- drawing


class Canvas:
    """Draws in unit coordinates onto an RGBA layer of size (w, h) * SS."""

    def __init__(self, w, h, scale, ox, oy):
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.scale = scale * SS
        self.ox, self.oy = ox * SS, oy * SS

    def p(self, x, y):
        return (self.ox + x * self.scale, self.oy + y * self.scale)

    def pts(self, pts):
        return [self.p(x, y) for x, y in pts]

    def layer(self):
        return Image.new("RGBA", self.img.size, (0, 0, 0, 0))

    def put(self, layer, mask=None):
        if mask is not None:
            a = ImageChops.multiply(layer.getchannel("A"), mask)
            layer.putalpha(a)
        self.img = Image.alpha_composite(self.img, layer)

    def u(self, v):
        return v * self.scale


def draw_glass(cv: Canvas, d: dict, color: tuple, flags: dict):
    g = GLASSES[d["glass"]]
    if "opaque" in g:
        return draw_opaque(cv, g, color, flags)
    pts = bowl_points(g["bowl"])
    rim_y = pts[0][0]
    bottom_y = pts[-1][0]

    # stem and foot
    if g.get("stem"):
        L = cv.layer(); dr = ImageDraw.Draw(L)
        y0, y1 = g["stem"]
        dr.rectangle([*cv.p(48.6, y0), *cv.p(51.4, y1)], fill=GLASS_EDGE[:3] + (150,))
        f = g["foot"]
        dr.ellipse([*cv.p(50 - f, y1 - 1.8), *cv.p(50 + f, y1 + 2.6)], fill=(220, 214, 204, 150),
                   outline=GLASS_EDGE, width=max(1, int(cv.u(0.9))))
        cv.put(L)
    if g.get("handle"):
        L = cv.layer(); dr = ImageDraw.Draw(L)
        w = width_at(pts, 30)
        dr.arc([*cv.p(50 + w - 8, 22), *cv.p(50 + w + 12, 54)], start=-90, end=90,
               fill=GLASS_EDGE, width=int(cv.u(3.2)))
        cv.put(L)

    # glass body tint
    body = polygon(pts)
    if g.get("base"):
        b = g["base"]
        wb = pts[-1][1]
        L = cv.layer(); dr = ImageDraw.Draw(L)
        dr.rounded_rectangle([*cv.p(50 - wb, bottom_y), *cv.p(50 + wb, bottom_y + b)],
                             radius=cv.u(1.5), fill=(225, 220, 210, 120))
        cv.put(L)
    L = cv.layer(); ImageDraw.Draw(L).polygon(cv.pts(body), fill=GLASS_FILL)
    cv.put(L)

    # liquid
    fill_y = g["fill"]
    liquid_mask = Image.new("L", cv.img.size, 0)
    ImageDraw.Draw(liquid_mask).polygon(cv.pts(polygon(pts, fill_y)), fill=255)
    draw_liquid(cv, liquid_mask, pts, fill_y, bottom_y, color, flags)

    # rim decorations under the outline
    if flags.get("rim"):
        L = cv.layer(); dr = ImageDraw.Draw(L)
        w = pts[0][1]
        col = (250, 250, 250, 235) if flags["rim"] == "salt" else (240, 224, 180, 235)
        for k in range(26):
            x = 50 - w + 2 * w * k / 25
            dr.ellipse([*cv.p(x - 1.2, rim_y - 0.6), *cv.p(x + 1.2, rim_y + 2.6)], fill=col)
        cv.put(L)

    # outline and highlight
    L = cv.layer(); dr = ImageDraw.Draw(L)
    dr.line(cv.pts(body + body[:1]), fill=GLASS_EDGE, width=max(2, int(cv.u(1.1))), joint="curve")
    hx = [(50 - width_at(pts, y) + 3.5, y) for y in [rim_y + 4 + k for k in range(int((bottom_y - rim_y) * 0.55))]]
    if len(hx) > 2:
        dr.line(cv.pts(hx), fill=HIGHLIGHT, width=max(2, int(cv.u(1.6))))
    cv.put(L)
    return pts, rim_y, fill_y


def draw_liquid(cv, mask, pts, fill_y, bottom_y, color, flags):
    L = cv.layer(); dr = ImageDraw.Draw(L)
    x0, x1 = cv.p(0, 0)[0], cv.p(100, 0)[0]
    # vertical gradient: slightly darker toward the bottom
    steps = 24
    for k in range(steps):
        y0 = fill_y + (bottom_y - fill_y) * k / steps
        y1 = fill_y + (bottom_y - fill_y) * (k + 1) / steps
        c = shade(color, 1.0 - 0.18 * k / steps)
        dr.rectangle([x0, cv.p(0, y0)[1], x1, cv.p(0, y1)[1] + 1], fill=c + (235,))
    # layered drink (B-52 and friends): equal bands, first ingredient at the bottom
    if flags.get("layers"):
        n = len(flags["layers"])
        for k, lc in enumerate(flags["layers"]):
            y1 = bottom_y - (bottom_y - fill_y) * k / n
            y0 = bottom_y - (bottom_y - fill_y) * (k + 1) / n
            dr.rectangle([x0, cv.p(0, y0)[1], x1, cv.p(0, y1)[1] + 1], fill=lc + (245,))
    # sink layer (grenadine etc.) at the bottom, with a soft blend
    if flags.get("sink"):
        sc = flags["sink"]
        top = fill_y + (bottom_y - fill_y) * 0.55
        for k in range(16):
            y = top + (bottom_y - top) * k / 16
            a = int(235 * min(1, (k + 1) / 6))
            dr.rectangle([x0, cv.p(0, y)[1], x1, cv.p(0, bottom_y)[1] + 1], fill=sc + (a // 3,))
    # float layer on top
    if flags.get("float"):
        fc = flags["float"]
        h = max(4.0, (bottom_y - fill_y) * 0.22)
        dr.rectangle([x0, cv.p(0, fill_y)[1], x1, cv.p(0, fill_y + h)[1]], fill=fc + (245,))
    # foam
    if flags.get("foam"):
        h = max(3.0, (bottom_y - fill_y) * 0.16)
        dr.rectangle([x0, cv.p(0, fill_y)[1], x1, cv.p(0, fill_y + h)[1]], fill=(246, 242, 230, 245))
    cv.put(L, mask)

    # ice
    ice = flags.get("ice")
    if ice in ("cubes", "large_cube", "crushed", "blended"):
        L = cv.layer(); dr = ImageDraw.Draw(L)
        w = width_at(pts, fill_y)
        if ice == "large_cube":
            s = min(w * 1.1, (bottom_y - fill_y) * 0.8)
            cx, cy = 50, fill_y + s / 2 + 1
            dr.rounded_rectangle([*cv.p(cx - s / 2, cy - s / 2), *cv.p(cx + s / 2, cy + s / 2)],
                                 radius=cv.u(2), fill=(255, 255, 255, 60), outline=(255, 255, 255, 120),
                                 width=max(1, int(cv.u(0.7))))
        elif ice == "cubes":
            s = min(w * 0.62, 13)
            spots = [(-0.45, 0.35), (0.38, 0.25), (-0.05, 0.95), (0.45, 1.25), (-0.5, 1.5)]
            for fx, fy in spots:
                cx, cy = 50 + fx * w, fill_y + s * fy
                if cy + s / 2 > bottom_y:
                    continue
                dr.rounded_rectangle([*cv.p(cx - s / 2, cy - s / 2), *cv.p(cx + s / 2, cy + s / 2)],
                                     radius=cv.u(1.6), fill=(255, 255, 255, 55),
                                     outline=(255, 255, 255, 110), width=max(1, int(cv.u(0.6))))
        else:
            n = 40 if ice == "crushed" else 26
            rnd = _rng(int(fill_y * 31 + w * 7))
            for _ in range(n):
                y = fill_y + rnd() * (bottom_y - fill_y)
                ww = width_at(pts, y)
                x = 50 + (rnd() * 2 - 1) * ww * 0.85
                r = 1.6 + rnd() * 1.6 if ice == "crushed" else 0.8 + rnd() * 0.8
                dr.ellipse([*cv.p(x - r, y - r), *cv.p(x + r, y + r)], fill=(255, 255, 255, 70 if ice == "crushed" else 90))
        cv.put(L, mask)

    # bubbles
    if flags.get("bubbly"):
        L = cv.layer(); dr = ImageDraw.Draw(L)
        rnd = _rng(int(fill_y * 13 + bottom_y))
        for _ in range(14):
            y = fill_y + 2 + rnd() * (bottom_y - fill_y - 3)
            ww = width_at(pts, y)
            x = 50 + (rnd() * 2 - 1) * ww * 0.7
            r = 0.6 + rnd() * 0.7
            dr.ellipse([*cv.p(x - r, y - r), *cv.p(x + r, y + r)], outline=(255, 255, 255, 170),
                       width=max(1, int(cv.u(0.35))))
        cv.put(L, mask)

    # surface ellipse
    L = cv.layer(); dr = ImageDraw.Draw(L)
    w = width_at(pts, fill_y)
    top = flags.get("float") or ((246, 242, 230) if flags.get("foam") else color)
    dr.ellipse([*cv.p(50 - w, fill_y - 1.6), *cv.p(50 + w, fill_y + 1.6)], fill=shade(top, 1.18) + (240,))
    cv.put(L)


def _rng(seed):
    state = [seed & 0x7FFFFFFF or 1]

    def r():
        state[0] = (state[0] * 1103515245 + 12345) & 0x7FFFFFFF
        return state[0] / 0x7FFFFFFF
    return r


def draw_opaque(cv, g, color, flags):
    L = cv.layer(); dr = ImageDraw.Draw(L)
    if g["opaque"] == "copper":
        top, bot, wt, wb = 22, 86, 24, 24
        body = [(50 - wt, top), (50 + wt, top), (50 + wb, bot), (50 - wb, bot)]
        dr.arc([*cv.p(50 + wt - 6, 34), *cv.p(50 + wt + 14, 70)], start=-90, end=90,
               fill=(166, 92, 52, 255), width=int(cv.u(4)))
        dr.polygon(cv.pts(body), fill=(196, 112, 62, 255))
        # vertical shading bands
        for k, a in ((0.18, 70), (0.32, 40), (0.72, 50)):
            x = 50 - wt + 2 * wt * k
            dr.rectangle([*cv.p(x, top + 2), *cv.p(x + 3, bot - 2)], fill=(255, 210, 170, a))
        dr.rectangle([*cv.p(50 - wb, bot - 5), *cv.p(50 + wb, bot)], fill=(150, 80, 44, 255))
        rim_y, w = top, wt
    else:  # tiki
        top, bot, wt, wb = 16, 88, 21, 25
        body = [(50 - wt, top), (50 + wt, top), (50 + wb, bot), (50 - wb, bot)]
        dr.polygon(cv.pts(body), fill=(120, 74, 44, 255))
        dark = (70, 40, 22, 255)
        # carved face: brow, eyes, nose, mouth
        dr.rectangle([*cv.p(32, 34), *cv.p(68, 38)], fill=dark)
        dr.ellipse([*cv.p(34, 40), *cv.p(46, 50)], fill=dark)
        dr.ellipse([*cv.p(54, 40), *cv.p(66, 50)], fill=dark)
        dr.polygon(cv.pts([(50, 46), (45, 62), (55, 62)]), fill=dark)
        dr.rounded_rectangle([*cv.p(34, 66), *cv.p(66, 76)], radius=cv.u(3), fill=dark)
        for x in range(37, 64, 5):
            dr.rectangle([*cv.p(x, 67), *cv.p(x + 2.2, 75)], fill=(230, 214, 180, 255))
        rim_y, w = top, wt
    # liquid peeking at the rim
    dr.ellipse([*cv.p(50 - w, rim_y - 3), *cv.p(50 + w, rim_y + 3)], fill=shade(color, 1.1) + (255,))
    if flags.get("ice") in ("cubes", "crushed", "large_cube"):
        rnd = _rng(77)
        for _ in range(7):
            x = 50 + (rnd() * 2 - 1) * w * 0.7
            dr.rounded_rectangle([*cv.p(x - 3, rim_y - 3.5), *cv.p(x + 3, rim_y + 1)], radius=cv.u(1),
                                 fill=(255, 255, 255, 120))
    dr.line(cv.pts(body + body[:1]), fill=(40, 24, 14, 200), width=max(2, int(cv.u(1))))
    cv.put(L)
    pts = [(rim_y, w), (bot, wb)]
    return pts, rim_y, rim_y


# --------------------------------------------------------------------------- garnish


def draw_garnish(cv: Canvas, kind: str, pts, rim_y, fill_y, d: dict):
    if kind in ("none", "cream_float", "salt_rim", "sugar_rim"):
        return
    L = cv.layer(); dr = ImageDraw.Draw(L)
    w = pts[0][1]
    rx, ry = 50 + w, rim_y  # right rim point
    lw = max(1, int(cv.u(0.8)))

    def wheel(cx, cy, r, fruit):
        flesh, rind = FRUIT[fruit]
        dr.ellipse([*cv.p(cx - r, cy - r), *cv.p(cx + r, cy + r)], fill=rind + (255,))
        dr.ellipse([*cv.p(cx - r * 0.84, cy - r * 0.84), *cv.p(cx + r * 0.84, cy + r * 0.84)], fill=flesh + (255,))
        for k in range(8):
            a = k * math.pi / 4
            dr.line([cv.p(cx, cy), cv.p(cx + math.cos(a) * r * 0.8, cy + math.sin(a) * r * 0.8)],
                    fill=shade(flesh, 1.25) + (255,), width=lw)
        # notch so it sits on the rim
        dr.rectangle([*cv.p(cx - 0.6, cy), *cv.p(cx + 0.6, cy + r)], fill=(0, 0, 0, 0))

    def leaf(cx, cy, length, angle, col):
        pts_ = []
        for k in range(13):
            t = k / 12
            off = math.sin(t * math.pi) * length * 0.28
            pts_.append((t * length, off))
        pts_ += [(t, -o) for t, o in reversed(pts_)]
        ca, sa = math.cos(angle), math.sin(angle)
        poly = [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in pts_]
        dr.polygon(cv.pts(poly), fill=col + (255,))
        dr.line([cv.p(cx, cy), cv.p(cx + length * ca * 0.9, cy + length * sa * 0.9)], fill=shade(col, 0.7) + (255,), width=lw)

    if kind in ("lime_wheel", "lemon_wheel", "orange_slice", "cucumber"):
        fruit = {"lime_wheel": "lime", "lemon_wheel": "lemon", "orange_slice": "orange", "cucumber": "cucumber"}[kind]
        wheel(rx - 2, ry - 1, 10.5, fruit)
    elif kind in ("lime_wedge", "apple", "ginger"):
        fruit = {"lime_wedge": "lime", "apple": "apple", "ginger": "ginger"}[kind]
        flesh, rind = FRUIT[fruit]
        box = [*cv.p(rx - 11, ry - 11), *cv.p(rx + 9, ry + 9)]
        dr.pieslice(box, start=180, end=360, fill=rind + (255,))
        box2 = [*cv.p(rx - 9, ry - 9), *cv.p(rx + 7, ry + 7)]
        dr.pieslice(box2, start=180, end=360, fill=flesh + (255,))
    elif kind in ("lemon_twist", "orange_twist"):
        col = FRUIT["lemon" if kind == "lemon_twist" else "orange"][1]
        # a peel spiral hooked over the rim, seen side on
        spiral = [(rx + 1 + 3.2 * math.sin(t * 3.4 * math.pi), ry - 9 + 20 * t) for t in [k / 40 for k in range(41)]]
        dr.line(cv.pts(spiral), fill=col + (255,), width=int(cv.u(2.0)), joint="curve")
        dr.line(cv.pts(spiral[:8]), fill=shade(col, 1.3) + (255,), width=int(cv.u(0.9)))
    elif kind == "cherry":
        cx, cy = rx - 1, ry - 4
        dr.line([cv.p(cx, cy - 3), cv.p(cx + 5, cy - 13)], fill=(90, 120, 40, 255), width=int(cv.u(1.2)))
        dr.ellipse([*cv.p(cx - 5, cy - 4), *cv.p(cx + 5, cy + 6)], fill=(170, 18, 36, 255))
        dr.ellipse([*cv.p(cx - 3, cy - 2), *cv.p(cx - 0.5, cy + 0.5)], fill=(255, 140, 150, 200))
    elif kind in ("olive", "onion"):
        col = (122, 150, 52) if kind == "olive" else (244, 240, 228)
        x0, y0 = 50 - w * 0.7, rim_y - 10
        x1, y1 = 50 + w * 0.2, fill_y + 14
        dr.line([cv.p(x0, y0), cv.p(x1, y1)], fill=(205, 160, 90, 255), width=int(cv.u(1)))
        for t in (0.55, 0.75):
            cx, cy = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            dr.ellipse([*cv.p(cx - 3.6, cy - 3), *cv.p(cx + 3.6, cy + 3)], fill=col + (255,))
            if kind == "olive":
                dr.ellipse([*cv.p(cx - 1.2, cy - 1), *cv.p(cx + 1.2, cy + 1)], fill=(190, 40, 30, 255))
    elif kind in ("mint", "basil"):
        col = (70, 160, 70) if kind == "mint" else (60, 130, 50)
        cx, cy = rx - 6, ry - 1
        for ang, ln in ((-2.2, 11), (-1.55, 12), (-0.9, 11), (-1.9, 8), (-1.2, 8)):
            leaf(cx, cy, ln, ang, col if ln > 9 else shade(col, 1.2))
    elif kind == "pineapple":
        cx, cy = rx - 2, ry
        dr.polygon(cv.pts([(cx - 9, cy), (cx + 9, cy), (cx, cy - 12)]), fill=(246, 206, 70, 255))
        dr.line([cv.p(cx - 9, cy), cv.p(cx + 9, cy)], fill=(176, 120, 40, 255), width=int(cv.u(1.6)))
        leaf(cx, cy - 11, 9, -2.0, (70, 140, 60))
        leaf(cx, cy - 11, 9, -1.1, (80, 160, 70))
    elif kind == "coffee_beans":
        for k, dx in enumerate((-6, 0, 6)):
            cx, cy = 50 + dx, fill_y + (0.4 if k != 1 else -0.6)
            dr.ellipse([*cv.p(cx - 2.6, cy - 1.8), *cv.p(cx + 2.6, cy + 1.8)], fill=(70, 40, 22, 255))
            dr.line([cv.p(cx - 1.8, cy), cv.p(cx + 1.8, cy)], fill=(140, 90, 50, 255), width=lw)
    elif kind == "nutmeg":
        rnd = _rng(5)
        ww = width_at(pts, fill_y)
        for _ in range(22):
            x = 50 + (rnd() * 2 - 1) * ww * 0.75
            y = fill_y + (rnd() * 2 - 1) * 1.2
            dr.ellipse([*cv.p(x - 0.6, y - 0.6), *cv.p(x + 0.6, y + 0.6)], fill=(120, 72, 36, 255))
    elif kind == "cinnamon":
        dr.line([cv.p(50 + w * 0.2, fill_y + 10), cv.p(50 + w + 4, rim_y - 14)], fill=(140, 82, 40, 255), width=int(cv.u(3)))
        dr.line([cv.p(50 + w * 0.2 + 1, fill_y + 10), cv.p(50 + w + 5, rim_y - 14)], fill=(176, 110, 60, 255), width=int(cv.u(1)))
    elif kind == "celery":
        dr.line([cv.p(50 + w * 0.1, fill_y + 14), cv.p(50 + w * 0.6, rim_y - 16)], fill=(150, 200, 90, 255), width=int(cv.u(3.2)))
        leaf(50 + w * 0.6, rim_y - 16, 8, -2.3, (80, 160, 60))
        leaf(50 + w * 0.6, rim_y - 16, 8, -0.9, (90, 170, 70))
    elif kind == "berries":
        for dx, dy, col in ((-4, -3, (70, 20, 60)), (2, -6, (120, 20, 50)), (4, -1, (80, 24, 70))):
            cx, cy = rx + dx - 2, ry + dy
            dr.ellipse([*cv.p(cx - 3.4, cy - 3.4), *cv.p(cx + 3.4, cy + 3.4)], fill=col + (255,))
            dr.ellipse([*cv.p(cx - 1.6, cy - 1.8), *cv.p(cx - 0.4, cy - 0.6)], fill=(255, 255, 255, 110))
    elif kind == "chili":
        pts_ = [(rx - 3 + 9 * t, ry - 12 + 14 * t + math.sin(t * 3) * 2) for t in [k / 10 for k in range(11)]]
        dr.line(cv.pts(pts_), fill=(200, 30, 30, 255), width=int(cv.u(3)), joint="curve")
        dr.line([cv.p(rx - 3, ry - 12), cv.p(rx - 5, ry - 15)], fill=(70, 130, 50, 255), width=int(cv.u(1.4)))
    elif kind == "passion_fruit":
        cx, cy = 50, fill_y - 0.5
        dr.pieslice([*cv.p(cx - 8, cy - 8), *cv.p(cx + 8, cy + 8)], start=180, end=360, fill=(110, 40, 60, 255))
        dr.pieslice([*cv.p(cx - 6.5, cy - 6.5), *cv.p(cx + 6.5, cy + 6.5)], start=180, end=360, fill=(246, 190, 40, 255))
        rnd = _rng(9)
        for _ in range(9):
            a = math.pi + rnd() * math.pi
            r = rnd() * 5
            x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
            dr.ellipse([*cv.p(x - 0.7, y - 0.7), *cv.p(x + 0.7, y + 0.7)], fill=(40, 24, 20, 255))
    elif kind == "watermelon":
        box = [*cv.p(rx - 11, ry - 11), *cv.p(rx + 9, ry + 9)]
        dr.pieslice(box, start=180, end=360, fill=(70, 140, 60, 255))
        box2 = [*cv.p(rx - 9.5, ry - 9.5), *cv.p(rx + 7.5, ry + 7.5)]
        dr.pieslice(box2, start=180, end=360, fill=(236, 80, 90, 255))
        for k in range(4):
            x = rx - 6 + k * 3.5
            dr.ellipse([*cv.p(x - 0.6, ry - 4), *cv.p(x + 0.6, ry - 2.6)], fill=(30, 20, 20, 255))
    cv.put(L)


# --------------------------------------------------------------------------- public API


@lru_cache(maxsize=4)
def background(w: int, h: int) -> Image.Image:
    bg = Image.new("RGB", (w, h))
    dr = ImageDraw.Draw(bg)
    for y in range(h):
        t = y / max(1, h - 1)
        dr.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))
    # soft warm glow behind the glass
    glow = Image.new("L", (w, h), 0)
    ImageDraw.Draw(glow).ellipse([w * 0.2, h * 0.1, w * 0.8, h * 0.95], fill=60)
    glow = glow.filter(ImageFilter.GaussianBlur(min(w, h) / 6))
    warm = Image.new("RGB", (w, h), (120, 80, 45))
    return Image.composite(warm, bg, glow)


def render(d: dict, flags: dict, w: int, h: int) -> Image.Image:
    color = hex_rgb(d["color"]["hex"])
    scale = h / 110
    ox = (w - 100 * scale) / 2
    oy = 12 * scale
    cv = Canvas(w, h, scale, ox, oy)
    # counter shadow
    L = cv.layer(); ImageDraw.Draw(L).ellipse([*cv.p(22, 87), *cv.p(78, 95)], fill=(0, 0, 0, 90))
    cv.put(L.filter(ImageFilter.GaussianBlur(cv.u(1.5))))
    pts, rim_y, fill_y = draw_glass(cv, d, color, flags)
    draw_garnish(cv, d["garnish_icon"], pts, rim_y, fill_y, d)
    fg = cv.img.resize((w, h), Image.LANCZOS)
    out = background(w, h).convert("RGBA")
    out = Image.alpha_composite(out, fg).convert("RGB")
    return out


def icon_flags(d: dict, objective_flavors: list[str]) -> dict:
    ings = d["ings"]
    flags = {"ice": d["ice"], "bubbly": "bubbly" in objective_flavors}
    for i in ings:
        if "sink" in i.flags:
            flags["sink"] = LAYER_COLORS.get(i.ingredient_id, (150, 30, 50))
        if "float" in i.flags:
            flags["float"] = LAYER_COLORS.get(i.ingredient_id, (230, 220, 200))
        if "rim" in i.flags:
            flags["rim"] = "salt" if "salt" in i.ingredient_id else "sugar"
    if any(i.ingredient_id == "egg_white" and not i.optional for i in ings) or d["id"] in ("ramos_fizz",):
        flags["foam"] = True
    if d["method"] == "layered":
        flags["layers"] = [LAYER_COLORS.get(i.ingredient_id, (200, 160, 100)) for i in ings]
    if d["garnish_icon"] in ("salt_rim", "sugar_rim") and "rim" not in flags:
        flags["rim"] = "salt" if d["garnish_icon"] == "salt_rim" else "sugar"
    if d["garnish_icon"] == "cream_float" and "float" not in flags:
        flags["float"] = LAYER_COLORS["heavy_cream"]
    return flags


def save_png(img: Image.Image, path, colors: int = 64) -> None:
    q = img.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.save(path, optimize=True)
