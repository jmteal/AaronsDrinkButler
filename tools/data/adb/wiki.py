"""Wikidata (CC0), Wikipedia and Wikimedia Commons lookups. All calls are cached."""

from __future__ import annotations

import hashlib
import html
import json
import re
import unicodedata
from urllib.parse import quote, urlencode

from . import http

WD_API = "https://www.wikidata.org/w/api.php"
WP_API = "https://en.wikipedia.org/w/api.php"
COMMONS_API = "https://commons.wikimedia.org/w/api.php"
SPARQL = "https://query.wikidata.org/sparql"

# Wikidata classes that mean "this item is not a drink" (disambiguation pages, brands).
NOT_DRINK_CLASSES = {
    "Q4167410",   # Wikimedia disambiguation page
    "Q80359036",  # alcohol brand
    "Q167270",    # trademark
    "Q431289",    # brand
}


def norm(s: str) -> str:
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    s = s.lower().replace("&", "and")
    s = re.sub(r"\(.*?\)", "", s)
    s = re.sub(r"[^a-z0-9]+", " ", s)
    s = re.sub(r"\b(cocktail|the|drink)\b", " ", s)
    return re.sub(r"\s+", " ", s).strip()


def _api(base: str, ns: str, **params) -> dict:
    params = {"format": "json", "formatversion": "2", **params}
    return http.get_json(ns, base + "?" + urlencode(params)) or {}


def entities_by_enwiki(titles: list[str]) -> dict[str, dict]:
    """enwiki title -> Wikidata entity (follows redirects)."""
    out: dict[str, dict] = {}
    titles = sorted(set(titles))
    for k in range(0, len(titles), 40):
        chunk = titles[k:k + 40]
        data = _api(WD_API, "wikidata", action="wbgetentities", sites="enwiki",
                    titles="|".join(chunk), props="claims|sitelinks|labels|aliases",
                    languages="en", sitefilter="enwiki|commonswiki", redirects="yes")
        ents = data.get("entities", {})
        ents = ents.values() if isinstance(ents, dict) else ents
        for e in ents:
            if "missing" in e:
                continue
            t = e.get("sitelinks", {}).get("enwiki", {}).get("title")
            if t:
                out[t] = e
        # Map the requested titles (possibly redirects) to the resolved ones.
        q = _api(WP_API, "wikipedia", action="query", titles="|".join(chunk), redirects="1")
        for r in q.get("query", {}).get("redirects", []) + q.get("query", {}).get("normalized", []):
            if r["to"] in out:
                out[r["from"]] = out[r["to"]]
    return out


def entities_by_qid(qids: list[str]) -> dict[str, dict]:
    out = {}
    qids = sorted(set(qids))
    for k in range(0, len(qids), 40):
        data = _api(WD_API, "wikidata", action="wbgetentities", ids="|".join(qids[k:k + 40]),
                    props="claims|sitelinks|labels|aliases", languages="en",
                    sitefilter="enwiki|commonswiki")
        ents = data.get("entities", {})
        for e in (ents.values() if isinstance(ents, dict) else ents):
            if "missing" not in e:
                out[e["id"]] = e
    return out


def claim_values(e: dict, prop: str) -> list:
    vals = []
    for c in e.get("claims", {}).get(prop, []):
        dv = c.get("mainsnak", {}).get("datavalue", {})
        v = dv.get("value")
        if isinstance(v, dict) and "id" in v:
            vals.append(v["id"])
        elif v is not None:
            vals.append(v)
    return vals


def cocktail_index() -> list[dict]:
    """Every Wikidata item that is an instance of (a subclass of) cocktail or mixed drink,
    with English labels and aliases. Used for drinks without a Wikipedia title."""
    query = """
SELECT ?item ?label (GROUP_CONCAT(DISTINCT ?alias; separator="|") AS ?aliases) WHERE {
  ?item wdt:P31/wdt:P279? wd:Q134768 .
  ?item rdfs:label ?label FILTER(LANG(?label) = "en")
  OPTIONAL { ?item skos:altLabel ?alias FILTER(LANG(?alias) = "en") }
} GROUP BY ?item ?label
"""
    data = http.get_json("sparql", SPARQL + "?" + urlencode({"query": query, "format": "json"}),
                         headers={"Accept": "application/sparql-results+json"})
    out = []
    for b in (data or {}).get("results", {}).get("bindings", []):
        qid = b["item"]["value"].rsplit("/", 1)[-1]
        names = [b["label"]["value"]] + [a for a in b.get("aliases", {}).get("value", "").split("|") if a]
        out.append({"qid": qid, "label": b["label"]["value"], "names": names})
    return out


def label(e: dict) -> str | None:
    return e.get("labels", {}).get("en", {}).get("value")


def labels_for(qids: list[str]) -> dict[str, str]:
    ents = entities_by_qid(qids)
    return {q: label(e) or q for q, e in ents.items()}


# --------------------------------------------------------------------------- Wikipedia


def page_images(titles: list[str]) -> dict[str, str]:
    """enwiki title -> lead image file name (free images only)."""
    out = {}
    titles = sorted(set(titles))
    for k in range(0, len(titles), 40):
        chunk = titles[k:k + 40]
        data = _api(WP_API, "wikipedia", action="query", prop="pageimages", piprop="name",
                    pilicense="free", titles="|".join(chunk), redirects="1")
        q = data.get("query", {})
        back = {}
        for r in q.get("normalized", []) + q.get("redirects", []):
            back.setdefault(r["to"], []).append(r["from"])
        for p in q.get("pages", []):
            if p.get("pageimage"):
                for t in [p["title"]] + back.get(p["title"], []):
                    out[t] = p["pageimage"]
                    for t2 in back.get(t, []):
                        out[t2] = p["pageimage"]
    return out


# --------------------------------------------------------------------------- Commons

ALLOWED_LICENSE = re.compile(
    r"^(cc0|cc-zero|pd|pd-.*|public domain|cc-by-[1-4]\.0.*|cc-by-sa-[1-4]\.0.*|cc-by-2\.5.*|cc-by-sa-2\.5.*)$", re.I)
ALLOWED_SHORT = re.compile(r"^(CC0( 1\.0)?|Public domain|PD.*|CC BY(-SA)? [1-4]\.[05]( [A-Za-z\-]+)?)$", re.I)
ALLOWED_CATEGORY = re.compile(r"^Category:(CC-Zero|CC0|Public domain|PD[- ].*|CC-BY(-SA)?-[1-4]\.[05](-[a-z]+)?)$", re.I)
REJECT_HINTS = re.compile(r"\b(NC|ND|non-?commercial|no ?deriv|fair use|non-free|GFDL)\b", re.I)


def _strip_html(s: str | None) -> str | None:
    if not s:
        return s
    s = re.sub(r"<[^>]+>", "", s)
    s = html.unescape(s)
    return re.sub(r"\s+", " ", s).strip()


def _file_record(p: dict) -> dict:
    ii = p["imageinfo"][0]
    md = {k2: v.get("value") for k2, v in ii.get("extmetadata", {}).items()}
    return {
        "title": p["title"],
        "url": ii.get("url"),
        "thumb": ii.get("thumburl") or ii.get("url"),
        "width": ii.get("width"), "height": ii.get("height"),
        "mime": ii.get("mime"),
        "desc_url": ii.get("descriptionurl"),
        "license_code": (md.get("License") or "").strip(),
        "license": _strip_html(md.get("LicenseShortName")),
        "license_url": md.get("LicenseUrl"),
        "usage_terms": _strip_html(md.get("UsageTerms")),
        "author": _strip_html(md.get("Artist")),
        "credit": _strip_html(md.get("Credit")),
        "attribution": _strip_html(md.get("Attribution")),
        "nonfree": md.get("NonFree"),
        "restrictions": md.get("Restrictions"),
        "categories": [c["title"] for c in p.get("categories", [])],
    }


def _file_cache_path(title: str):
    d = http.CACHE_DIR / "commons_files"
    d.mkdir(parents=True, exist_ok=True)
    return d / (hashlib.sha1(title.encode()).hexdigest() + ".json")


def _store_pages(q: dict) -> None:
    back: dict[str, list[str]] = {}
    for r in q.get("normalized", []) + q.get("redirects", []):
        back.setdefault(r["to"], []).append(r["from"])
    for p in q.get("pages", []):
        rec = _file_record(p) if ("missing" not in p and p.get("imageinfo")) else {"missing": True}
        for t in [p.get("title")] + back.get(p.get("title"), []):
            if t:
                _file_cache_path(t).write_text(json.dumps(rec, ensure_ascii=False))


def _seed_from_batches() -> None:
    """Index older batched responses so per-file lookups work offline."""
    for f in (http.CACHE_DIR / "commons").glob("*.json"):
        try:
            data = json.loads(f.read_text())
        except ValueError:
            continue
        q = (data or {}).get("query", {})
        if q.get("pages") and any("imageinfo" in p for p in q["pages"]):
            _store_pages(q)


def file_info(titles: list[str]) -> dict[str, dict]:
    """File:Title -> {url, thumb, width, height, mime, license, license_url, author, desc_url, ...}
    Cached per file, so changing the candidate list never invalidates the cache."""
    titles = sorted({t if t.startswith("File:") else "File:" + t for t in titles})
    if any(not _file_cache_path(t).exists() for t in titles):
        _seed_from_batches()
    todo = [t for t in titles if not _file_cache_path(t).exists()]
    for k in range(0, len(todo), 25):
        chunk = todo[k:k + 25]
        data = _api(COMMONS_API, "commons", action="query", prop="imageinfo|categories",
                    iiprop="url|size|mime|extmetadata", iiurlwidth="800", cllimit="max",
                    clshow="!hidden", titles="|".join(chunk), redirects="1")
        _store_pages(data.get("query", {}))
        for t in chunk:
            if not _file_cache_path(t).exists():
                _file_cache_path(t).write_text(json.dumps({"missing": True}))
    out = {}
    for t in titles:
        rec = json.loads(_file_cache_path(t).read_text())
        if not rec.get("missing"):
            out[t] = rec
    return out


def license_ok(info: dict) -> tuple[bool, str | None, str]:
    """(allowed, spdx-like id, reason). Multi-licensed files fall back to their license categories."""
    if str(info.get("nonfree", "")).lower() in ("true", "1", "yes"):
        return False, None, "non-free"
    code, short = info.get("license_code", ""), info.get("license") or ""
    if (ALLOWED_LICENSE.match(code) or ALLOWED_SHORT.match(short)) and not REJECT_HINTS.search(short):
        return True, spdx(code, short), "extmetadata"
    for c in info.get("categories", []):
        m = ALLOWED_CATEGORY.match(c)
        if m:
            name = c.split(":", 1)[1]
            return True, spdx(name.lower(), name.replace("-", " ")), f"multi-licensed: {c}"
    return False, None, f"license not on allowlist: {short or code or 'none'}"


def spdx(code: str, short: str) -> str:
    c = (code or "").lower()
    s = (short or "").lower()
    if c in ("cc0", "cc-zero") or "cc0" in s or "zero" in c:
        return "CC0-1.0"
    if c.startswith("pd") or "public domain" in s:
        return "Public-Domain"
    m = re.match(r"cc[- ]by(-sa)?[- ]([1-4]\.[05])(?:[- ]([a-z\-]+))?", c) or \
        re.match(r"cc by(-sa)? ([1-4]\.[05])(?: ([a-z\-]+))?", s)
    if m:
        sa = "-SA" if m.group(1) else ""
        port = f"-{m.group(3).upper()}" if m.group(3) else ""
        return f"CC-BY{sa}-{m.group(2)}{port}"
    return short or code


def license_url_for(spdx_id: str, given: str | None) -> str | None:
    if given:
        return given
    if spdx_id == "CC0-1.0":
        return "https://creativecommons.org/publicdomain/zero/1.0/"
    if spdx_id == "Public-Domain":
        return "https://creativecommons.org/publicdomain/mark/1.0/"
    m = re.match(r"CC-BY(-SA)?-([0-9.]+)", spdx_id)
    if m:
        kind = "by-sa" if m.group(1) else "by"
        return f"https://creativecommons.org/licenses/{kind}/{m.group(2)}/"
    return None


def category_files(category: str, limit: int = 30) -> list[str]:
    data = _api(COMMONS_API, "commons", action="query", list="categorymembers",
                cmtitle="Category:" + category, cmtype="file", cmlimit=str(limit))
    return [m["title"] for m in data.get("query", {}).get("categorymembers", [])]


def search_files(text: str, limit: int = 15) -> list[str]:
    data = _api(COMMONS_API, "commons", action="query", list="search", srnamespace="6",
                srsearch=f"{text} filetype:bitmap", srlimit=str(limit))
    return [m["title"] for m in data.get("query", {}).get("search", [])]


def commons_page_url(title: str) -> str:
    return "https://commons.wikimedia.org/wiki/" + quote(title.replace(" ", "_"))
