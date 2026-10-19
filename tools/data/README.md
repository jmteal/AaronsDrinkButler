# Recipe data pipeline

Builds `data/` (recipes, ingredients, photos, icons, attribution, review sheet) from our own
curated files. Runs on the Mac; the device only ever sees the output.

## Run it

```bash
cd tools/data
python3 -m venv .venv            # Python 3.11+ (tested with 3.13)
.venv/bin/pip install -r requirements.txt
.venv/bin/python build.py --offline   # rebuild from the cache (no network)
.venv/bin/python build.py             # fetch anything new, then build
```

Every network response (IBA pages, Wikidata, Wikipedia, Commons, TheCocktailDB, photo files)
is cached under `tools/data/.cache/` (git-ignored), so a rerun is offline and gives the same
output. `--offline` fails loudly on a cache miss instead of fetching. Requests send a
descriptive User-Agent and are throttled per host (1–1.5 s between calls).

A fresh clone has no cache, so the first `build.py` run fetches about 1,500 requests and takes
roughly 30–40 minutes because of the throttling.

## What you edit

| File | What it holds |
|---|---|
| `drinks_master.yaml` | **Source of truth.** Every drink: name, IBA category, glass, method, ice, garnish, ingredient specs, optional hand-written steps, Wikipedia title, spec doubts. Format notes are at the top of the file |
| `ingredients.yaml` | Normalized ingredients: name, category, ABV, base spirit, substitutes, pantry flags, aliases used to match reference data |
| `tags_proposed.yaml` | Flavor / season / occasion / colour **proposed by an LLM** (Claude), one line per drink |
| `tags_overrides.yaml` | Reviewed values; always win. Created by `build.py import-review` (or by hand) |
| `photo_overrides.yaml` | Per drink: a hand-picked Commons file, `none` to force the icon, `reject:` a bad pick, or `focus:` (0–1) to move the crop up or down |

## Stages (`build.py`)

1. **Load and validate** the master list and ingredient table (unknown ids, glasses, units fail the build).
2. **Wikidata** (CC0): resolve each drink's item from its Wikipedia title (or a cocktail-name index from SPARQL).
3. **Cross-check** (`adb/crosscheck.py`): IBA list membership and categories against iba-world.com and Wikipedia's list; our amounts against each IBA spec; ingredient sets against TheCocktailDB (test key `1`, dev only, nothing bundled) and Wikidata P186. Results go to `reports/REPORT.md`.
4. **Derive** (`adb/derive.py`): ABV estimate (dilution: shaken +25%, stirred +20%, built +10%, muddled +15%, blended +30%), strength, base spirit, alcoholic / low-ABV / mocktail, difficulty, and the objective flavors (bubbly, coffee, creamy, smoky). Then merge the proposed tags and any overrides.
5. **Steps** (`adb/steps.py`): short imperative steps generated from the structured spec, in our own words. A drink can supply its own `steps:` instead. Nothing is copied from IBA, Wikipedia or TheCocktailDB prose.
6. **Photos** (`adb/photos.py`): candidates from the override file, Wikidata P18, the Wikipedia lead image, the item's Commons category, then a Commons search whose file title must contain the drink name. Only **CC0, public domain, CC BY and CC BY-SA** pass (multi-licensed files may qualify through their license category); everything else is rejected and logged in `reports/photo_log.csv`. Accepted photos are centre-cropped and resized to a 320×240 baseline JPEG (q78) and a 96×96 JPEG (q80), with EXIF stripped.
7. **Icons** (`adb/icons.py`): a glass + liquid colour + ice + garnish drawing for every drink, 96×96 and 320×240 palette PNGs.
8. **Write** `data/recipes.json`, `data/ingredients.json`, `data/ATTRIBUTION.md`, `data/review/tags.csv` + `tags.html`, validate against `recipe.schema.json`, and write `reports/REPORT.md`.

## Device recipe pack

```bash
python3 build.py pack                 # -> firmware/build/drinks.bin, with spoken recipes
python3 build.py pack -o drinks.bin
python3 build.py pack --no-audio      # without speech: standard library only, no TTS setup
```

Packs `data/` into the image for the firmware's read-only `drinks` flash partition (format:
[`adb/pack.py`](adb/pack.py), reader: `firmware/components/data/src/adb_pack.c`). The pack
itself only reads `data/` and runs on a plain `python3` without the packages above. From
`firmware/`, `idf.py drinks-flash` runs it (with speech) and flashes the result, so editing a
recipe re-records just what changed and flashes it in one step.

## Speech ("speak the recipe")

Each drink's detail screen has a speaker button that reads the recipe aloud. The audio is
made here, offline, with [Parler-TTS](https://github.com/huggingface/parler-tts) Mini v1
(Apache 2.0), and stored in the pack as Opus at about 12 kbps.

**Setup is automatic.** On the first `build.py pack` it:

1. checks for `opusenc` and stops early with `brew install opus-tools` if it's missing;
2. creates `tools/data/.venv-tts` (git-ignored) with Python 3.11–3.13 (`python3.13` etc. on
   `PATH`, or set `ADB_TTS_PYTHON`) and installs [`requirements-tts.lock`](requirements-tts.lock):
   every package pinned, Parler-TTS and descript-audiotools as pinned GitHub tarballs
   (torch 2.6.0, transformers 4.46.1), about 1.5 GB;
3. downloads the model (`parler-tts/parler-tts-mini-v1`, pinned revision, about 3.5 GB) to the
   Hugging Face cache (`~/.cache/huggingface`);
4. records every sentence, then joins and encodes each drink.

**Run time.** On an M3 MacBook Air the model runs on the **CPU**; Apple's MPS backend works but
is about twice as slow for this model (one sentence at a time, autoregressive), so `--device
auto` uses the CPU on Apple silicon (`--device mps` to try it). Speech comes out at about
0.2–0.3x real time, so the first full run (200 drinks, ~2,300 sentences, ~90 min of audio)
takes several hours unattended. It prints progress and an ETA every 10 sentences.

**Caching.** Every sentence clip is kept in `tools/data/.cache/tts/clips/`, keyed by a hash of
the sentence text, the sound settings in `voice.yaml` (speaker, description, seed) and the
model and Parler-TTS revisions. Each drink's joined Opus file is keyed by its clip keys, the
pauses and the encoder settings. So:

- a second `pack` with nothing changed records nothing and encodes nothing (seconds);
- editing one recipe re-records only that recipe's new or changed sentences;
- changing pauses or the bitrate re-joins and re-encodes, without recording;
- changing the speaker, description or seed re-records everything.

**The script** ([`adb/speech.py`](adb/speech.py)): name, glass, "You will need:", each
ingredient with its amount spoken in ounces (quarter ounces, teaspoons under 7.5 ml, as the
device shows them), the steps and the garnish. Numbers and symbols are written out
("ten to fifteen seconds"). Sentences are recorded one at a time, because Parler drifts on long
inputs, and joined with short silences. Print a script with `python3 -m adb.speech negroni`.

**The persona** is [`voice.yaml`](voice.yaml): one of Parler's 34 named speakers (Laura, Gary,
Jon, Lea, Karen, Rick, Brenda, David, Eileen, Jordan, Mike, Yann, Joy, James, Eric, Lauren,
Rose, Will, Jason, Aaron, Naomie, Alisa, Patrick, Jerry, Tina, Jenna, Bill, Tom, Carol, Barbara,
Rebecca, Anna, Bruce, Emily), a description of how they speak (keep "Very clear audio." at the
end), a fixed seed (so the voice stays the same across clips), the pauses and the bitrate.
One persona per pack: a full set is about 8 MB, and the partition has room for one.

**Listening.** `python3 build.py say negroni` prints the script and plays the clip with
`afplay` (from the cache, so run `pack` first).

**Checking (`--verify`).** `build.py pack --verify` also installs
[`requirements-verify.lock`](requirements-verify.lock) (faster-whisper) into the venv,
transcribes each sentence with Whisper `small` on the CPU, and compares it with the script.
A sentence whose word error rate is above `verify_wer` is re-recorded with other seeds
(`seed + 1000`, `+ 2000`, ...) up to `verify_retries` times, keeping the best take; whatever
still fails is listed at the end. Results are cached with the clips, so a second `--verify`
only checks new clips. Expect some false alarms on drink and brand names (Whisper hears
"Campari" fine but not every amaro).

## Reviewing tags

Open `data/review/tags.html` in a browser to scan the proposed tags with each drink's picture.
To correct them, edit `data/review/tags.csv`, put `yes` in the `reviewed` column of each row you
checked, then:

```bash
.venv/bin/python build.py import-review
.venv/bin/python build.py --offline
```

## Fixing a photo

If a photo shows the wrong drink or crops badly, add to `photo_overrides.yaml`:

```yaml
dirty_martini: none                              # use the icon
gimlet: {reject: ["File:Bad gimlet.jpg"]}        # skip this file, try the next candidate
negroni: {file: "File:Negroni served in Vintage Cocktail Glass.jpg", focus: 0.35}
```

A new or changed pick needs one online run (`build.py` without `--offline`) to fetch it.

## Licensing rules (the repo is public)

- Our steps, notes and tags: CC BY 4.0. Specs (ingredients and amounts) are facts.
- Photos: Commons only, allowlisted licenses only, each credited in `data/ATTRIBUTION.md` (title, author, license, source URL, "cropped and resized").
- TheCocktailDB and IBA are references only: no text and no images from them are bundled. The raw cache stays out of git.
