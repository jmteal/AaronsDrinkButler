"""IBA official list: names, categories and ingredient specs, used only to verify our
own master list. IBA text and photos are copyrighted, so nothing from these pages is
written to data/ except facts (names, category, amounts) in the cross-check report."""

from __future__ import annotations

import html
import re

from . import http

LIST_URL = "https://iba-world.com/cocktails/all-cocktails/"
PAGE_URL = "https://iba-world.com/iba-cocktail/{slug}/"
WIKI_LIST_URL = (
    "https://en.wikipedia.org/w/api.php?action=parse&page=List_of_IBA_official_cocktails"
    "&prop=wikitext&format=json&formatversion=2"
)

CATEGORIES = {
    "the unforgettables": "unforgettable",
    "contemporary classics": "contemporary_classic",
    "new era": "new_era",
    "new era drinks": "new_era",
}


def _plain(s: str) -> str:
    s = re.sub(r"<[^>]+>", " ", s)
    return re.sub(r"\s+", " ", html.unescape(s)).strip()


def fetch_list() -> dict[str, dict]:
    """slug -> {name, category}"""
    out: dict[str, dict] = {}
    for page in range(1, 10):
        url = LIST_URL if page == 1 else f"{LIST_URL}page/{page}/"
        t = http.get_text("iba", url)
        if not t:
            break
        found = 0
        for m in re.finditer(
            r'href="https://iba-world.com/iba-cocktail/([^/"]+)/"(.{0,1500}?)views', t, re.S
        ):
            slug, chunk = m.group(1), _plain(m.group(2))
            cat = None
            for label, key in CATEGORIES.items():
                if re.search(label, chunk, re.I):
                    cat = key
                    name = re.split(label, chunk, flags=re.I)[0].strip()
                    break
            if cat and slug not in out:
                out[slug] = {"name": name, "category": cat}
                found += 1
        if found == 0:
            break
    return out


_ING = re.compile(
    r"^(?P<amt>[\d.,/½¼¾ ]+)?\s*(?P<unit>ml|cl|dash(?:es)?|drops?|bar ?spoons?|teaspoons?|tsp|"
    r"cubes?|slices?|leaves|leaf|sprigs?|pieces?|wedges?|tbsp|oz)?\.?\s*(?P<name>.+)$",
    re.I,
)


def _num(s: str | None) -> float | None:
    if not s:
        return None
    s = s.strip().replace(",", ".").replace("½", ".5").replace("¼", ".25").replace("¾", ".75")
    try:
        if "/" in s:
            a, b = s.split("/")
            return float(a) / float(b)
        return float(s)
    except ValueError:
        return None


def fetch_spec(slug: str) -> dict | None:
    t = http.get_text("iba", PAGE_URL.format(slug=slug))
    if not t:
        return None
    text = re.sub(r"<[^>]+>", "\n", t)
    text = html.unescape(text)
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    spec = {"ingredients": [], "garnish": None}
    try:
        i = lines.index("Ingredients")
    except ValueError:
        return spec
    j = i + 1
    while j < len(lines) and lines[j] not in ("Method", "Garnish"):
        line = lines[j]
        m = _ING.match(line)
        amt = _num(m.group("amt")) if m else None
        unit = (m.group("unit") or "").lower() if m else ""
        if unit == "cl" and amt is not None:
            amt, unit = amt * 10, "ml"
        spec["ingredients"].append(
            {"raw": line, "amount": amt, "unit": unit or None,
             "name": (m.group("name") if m else line).strip()}
        )
        j += 1
    if "Garnish" in lines[j:]:
        k = lines.index("Garnish", j)
        if k + 1 < len(lines):
            spec["garnish"] = lines[k + 1]
    return spec


def fetch_wikipedia_names() -> list[str]:
    data = http.get_json("wikipedia", WIKI_LIST_URL)
    if not data:
        return []
    t = data["parse"]["wikitext"]
    end = t.find("==Former drinks==")
    names = []
    for m in re.finditer(r"^;\s*(.+?)\s:", t[:end], re.M):
        n = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", m.group(1))
        names.append(n.strip())
    return names
