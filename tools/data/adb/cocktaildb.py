"""TheCocktailDB lookups with the public test key "1".

Dev-only cross-check: the test key is for development, and nothing from TheCocktailDB is
bundled in data/ (no text, no images). We only compare ingredient lists and glass/alcohol
flags and credit the site in ATTRIBUTION.md as a reference.
"""

from __future__ import annotations

from urllib.parse import quote

from . import http
from .wiki import norm

SEARCH = "https://www.thecocktaildb.com/api/json/v1/1/search.php?s="


def search(name: str) -> list[dict]:
    data = http.get_json("cocktaildb", SEARCH + quote(name))
    return (data or {}).get("drinks") or []


def find(names: list[str]) -> dict | None:
    """Exact (normalized) name match for any of the names."""
    wanted = {norm(n) for n in names}
    for n in names:
        for d in search(n):
            if norm(d.get("strDrink", "")) in wanted:
                return d
    return None


def ingredients(d: dict) -> list[tuple[str, str]]:
    out = []
    for k in range(1, 16):
        ing = (d.get(f"strIngredient{k}") or "").strip()
        if ing:
            out.append((ing, (d.get(f"strMeasure{k}") or "").strip()))
    return out


def page_url(d: dict) -> str:
    return f"https://www.thecocktaildb.com/drink/{d['idDrink']}"
