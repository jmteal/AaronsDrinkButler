"""Cached, rate-limited HTTP fetching.

Every response is stored under tools/data/.cache/<namespace>/ keyed by a hash of
the URL, so a rerun never touches the network for anything it has seen before.
Set ADB_OFFLINE=1 to fail on any cache miss instead of fetching.
"""

from __future__ import annotations

import hashlib
import json
import os
import time
from pathlib import Path
from urllib.parse import urlparse

import requests

from .paths import CACHE_DIR

USER_AGENT = (
    "AaronsDrinkButler-data/0.1 "
    "(https://github.com/jmteal/AaronsDrinkButler; offline cocktail dataset builder) "
    "python-requests"
)

# Minimum seconds between requests to the same host.
HOST_DELAY = {
    "www.thecocktaildb.com": 1.0,
    "query.wikidata.org": 1.5,
    "www.wikidata.org": 1.0,
    "commons.wikimedia.org": 1.0,
    "en.wikipedia.org": 1.0,
    "upload.wikimedia.org": 1.0,
    "iba-world.com": 1.5,
}
DEFAULT_DELAY = 1.0

_last_request: dict[str, float] = {}
_session: requests.Session | None = None
stats = {"hits": 0, "fetches": 0}


def offline() -> bool:
    return os.environ.get("ADB_OFFLINE", "") not in ("", "0")


def _sess() -> requests.Session:
    global _session
    if _session is None:
        _session = requests.Session()
        _session.headers["User-Agent"] = USER_AGENT
    return _session


def _path(namespace: str, url: str, ext: str) -> Path:
    key = hashlib.sha1(url.encode()).hexdigest()
    d = CACHE_DIR / namespace
    d.mkdir(parents=True, exist_ok=True)
    return d / f"{key}.{ext}"


def _throttle(url: str) -> None:
    host = urlparse(url).netloc
    delay = HOST_DELAY.get(host, DEFAULT_DELAY)
    wait = _last_request.get(host, 0) + delay - time.monotonic()
    if wait > 0:
        time.sleep(wait)
    _last_request[host] = time.monotonic()


def _fetch(url: str, headers: dict | None = None) -> requests.Response:
    if offline():
        raise RuntimeError(f"ADB_OFFLINE set and not cached: {url}")
    for attempt in range(5):
        _throttle(url)
        stats["fetches"] += 1
        try:
            r = _sess().get(url, headers=headers or {}, timeout=90)
        except requests.RequestException:
            if attempt == 4:
                raise
            time.sleep(10 * (attempt + 1))
            continue
        if r.status_code == 429 or r.status_code >= 500:
            retry = float(r.headers.get("Retry-After", "0") or 0)
            time.sleep(max(retry, 5 * (attempt + 1)))
            continue
        return r
    r.raise_for_status()
    return r


def get_bytes(namespace: str, url: str, ext: str = "bin") -> bytes | None:
    """Fetch raw bytes (cached). Returns None for a cached or live 404."""
    p = _path(namespace, url, ext)
    miss = p.with_suffix(".404")
    if p.exists():
        stats["hits"] += 1
        return p.read_bytes()
    if miss.exists():
        stats["hits"] += 1
        return None
    r = _fetch(url)
    if r.status_code == 404:
        miss.write_text(url)
        return None
    r.raise_for_status()
    p.write_bytes(r.content)
    p.with_suffix(".url").write_text(url)
    return r.content


def get_text(namespace: str, url: str) -> str | None:
    b = get_bytes(namespace, url, "txt")
    return None if b is None else b.decode("utf-8", errors="replace")


def get_json(namespace: str, url: str, headers: dict | None = None):
    p = _path(namespace, url, "json")
    if p.exists():
        stats["hits"] += 1
        return json.loads(p.read_text())
    r = _fetch(url, headers)
    if r.status_code == 404:
        return None
    r.raise_for_status()
    text = r.text.strip()
    data = json.loads(text) if text else None
    if isinstance(data, dict) and "error" in data and "code" in data["error"]:
        raise RuntimeError(f"API error for {url}: {data['error'].get('info')}")
    p.write_text(json.dumps(data, ensure_ascii=False))
    p.with_suffix(".url").write_text(url)
    return data
