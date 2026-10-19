"""Speech clips for the recipe pack ("speak the recipe"). Standard library only.

The heavy part (Parler-TTS on torch) runs in its own venv, tools/data/.venv-tts, which this
module creates on first use from requirements-tts.lock (exact pins, no resolver). The venv
runs tools/data/tts_worker.py, which records one clip per sentence into
.cache/tts/clips/. Here the clips are joined with pauses into one recording per drink,
encoded to Opus with opusenc, and repacked for the device:

    speech/<drink id>.opk   (all integers little-endian; reader:
                             firmware/components/data/src/adb_speech.c)
        char magic[4] "ADBS", u16 version (1), u16 pre_skip (samples at 16 kHz to drop),
        u32 samples (16 kHz, after pre_skip), u32 packet count,
        then per packet: u16 length, Opus packet bytes (mono, decoded at 16 kHz)

Both stages are cached: a sentence clip is keyed by its text, the sound-affecting voice
settings and the model revision; a drink's Opus file by its clip keys, the pauses and the
bitrate. A rerun with nothing changed does no TTS and no encoding.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

from . import speech
from .paths import CACHE_DIR, TOOLS_DIR

VENV = TOOLS_DIR / ".venv-tts"
LOCK = TOOLS_DIR / "requirements-tts.lock"
LOCK_VERIFY = TOOLS_DIR / "requirements-verify.lock"
WORKER = TOOLS_DIR / "tts_worker.py"
VOICE = TOOLS_DIR / "voice.yaml"
TTS_CACHE = CACHE_DIR / "tts"
RATE = 16000
OPK_MAGIC = b"ADBS"
OPK_VERSION = 1
OPK_HEADER = struct.Struct("<4sHHII")
# torch 2.6 has wheels for CPython 3.9-3.13 only.
PYTHONS = ("python3.13", "python3.12", "python3.11")
BREW_OPUS = "brew install opus-tools"


def need_opusenc() -> str:
    exe = shutil.which("opusenc")
    if not exe:
        raise SystemExit(f"pack: opusenc not found. Install it with:\n\n    {BREW_OPUS}\n\n"
                         "or build a pack without speech: build.py pack --no-audio")
    return exe


def _sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def venv_python() -> Path:
    return VENV / "bin" / "python"


def _base_python() -> str:
    env = os.environ.get("ADB_TTS_PYTHON")
    if env:
        return env
    for name in PYTHONS:
        exe = shutil.which(name)
        if exe:
            return exe
    raise SystemExit("pack: the TTS venv needs Python 3.11-3.13 (torch 2.6 has no wheels for newer). "
                     "Install one (brew install python@3.13) or set ADB_TTS_PYTHON, "
                     "or build without speech: build.py pack --no-audio")


def _install(lock: Path, stamp_name: str) -> None:
    stamp = VENV / stamp_name
    if stamp.exists() and stamp.read_text() == _sha(lock):
        return
    print(f"tts: installing {lock.name} into {VENV.relative_to(TOOLS_DIR.parent.parent)} "
          "(first run only; about 1.5 GB)", flush=True)
    pip = [str(venv_python()), "-m", "pip", "install", "--disable-pip-version-check", "--no-deps",
           "-r", str(lock)]
    subprocess.run(pip, check=True)
    stamp.write_text(_sha(lock))


def ensure_venv(verify: bool) -> Path:
    if not venv_python().exists():
        base = _base_python()
        print(f"tts: creating {VENV} with {base}", flush=True)
        subprocess.run([base, "-m", "venv", str(VENV)], check=True)
    _install(LOCK, ".adb-tts-lock")
    if verify:
        _install(LOCK_VERIFY, ".adb-verify-lock")
    return venv_python()


def run_worker(sentences: list[str], verify: bool, device: str) -> dict:
    py = ensure_venv(verify)
    TTS_CACHE.mkdir(parents=True, exist_ok=True)
    # Per-process job files, so two packs at once (e.g. a test build during a long run) don't
    # clobber each other. result.json is the last finished run, for `build.py say`.
    job = TTS_CACHE / f"job-{os.getpid()}.json"
    result = TTS_CACHE / f"result-{os.getpid()}.json"
    job.write_text(json.dumps({"sentences": sentences, "clips_dir": str(TTS_CACHE / "clips"),
                               "voice": str(VOICE), "verify": verify, "device": device}))
    result.unlink(missing_ok=True)
    env = dict(os.environ, PYTORCH_ENABLE_MPS_FALLBACK="1", TOKENIZERS_PARALLELISM="false",
               TRANSFORMERS_VERBOSITY="error", PYTHONWARNINGS="ignore")
    try:
        subprocess.run([str(py), str(WORKER), str(job), str(result)], check=True, env=env)
        os.replace(result, TTS_CACHE / "result.json")
    finally:
        job.unlink(missing_ok=True)
    return json.loads((TTS_CACHE / "result.json").read_text())


# ---- joining and encoding

def _read_pcm(p: Path) -> bytes:
    with wave.open(str(p), "rb") as w:
        assert w.getframerate() == RATE and w.getnchannels() == 1 and w.getsampwidth() == 2, p
        return w.readframes(w.getnframes())


def _silence(ms: int) -> bytes:
    return b"\0\0" * (RATE * ms // 1000)


def drink_key(keys: list[str], breaks: list[bool], voice: dict) -> str:
    ident = {"clips": keys, "breaks": breaks, "opusenc": OPUSENC_ARGS,
             **{k: voice[k] for k in ("pause_sentence", "pause_section", "lead_in", "tail", "opus_kbps")}}
    return hashlib.sha256(json.dumps(ident, sort_keys=True).encode()).hexdigest()[:24]


OPUSENC_ARGS = ["--quiet", "--speech", "--vbr", "--framesize", "60", "--comp", "10", "--serial", "1"]


def encode(opusenc: str, pcm: bytes, kbps: float, out: Path) -> None:
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td) / "in.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm)
        tmp = out.with_suffix(".tmp")
        subprocess.run([opusenc, *OPUSENC_ARGS, "--bitrate", str(kbps), str(wav), str(tmp)], check=True)
        os.replace(tmp, out)


def ogg_packets(data: bytes) -> list[bytes]:
    """Packets of a single-stream Ogg file, in order."""
    packets, cur, pos = [], b"", 0
    while pos < len(data):
        if data[pos:pos + 4] != b"OggS":
            raise ValueError("not an Ogg page")
        nseg = data[pos + 26]
        lace = data[pos + 27: pos + 27 + nseg]
        pos += 27 + nseg
        for n in lace:
            cur += data[pos:pos + n]
            pos += n
            if n < 255:
                packets.append(cur)
                cur = b""
    return packets


def ogg_last_granule(data: bytes) -> int:
    i = data.rfind(b"OggS")
    return struct.unpack_from("<q", data, i + 6)[0]


def to_opk(ogg: bytes) -> tuple[bytes, float]:
    """Ogg Opus -> device clip; also returns the duration in seconds."""
    pk = ogg_packets(ogg)
    head = pk[0]
    if head[:8] != b"OpusHead" or head[9] != 1:
        raise ValueError("expected mono Ogg Opus")
    pre_skip_48k = struct.unpack_from("<H", head, 10)[0]
    audio = pk[2:]                                   # skip OpusHead, OpusTags
    samples_48k = ogg_last_granule(ogg) - pre_skip_48k
    pre, samples = pre_skip_48k // 3, max(0, samples_48k) // 3
    body = b"".join(struct.pack("<H", len(p)) + p for p in audio)
    return OPK_HEADER.pack(OPK_MAGIC, OPK_VERSION, pre, samples, len(audio)) + body, samples / RATE


def build(recipes: list[dict], verify: bool = False, device: str = "auto") -> dict[str, bytes]:
    """speech/<id>.opk for every recipe. Prints progress and totals."""
    opusenc = need_opusenc()
    t0 = time.time()
    only = os.environ.get("ADB_TTS_ONLY")     # testing: comma-separated drink ids
    if only:
        recipes = [r for r in recipes if r["id"] in only.split(",")]
    scripts = {r["id"]: speech.parts(r) for r in recipes}
    sentences = [t for p in scripts.values() for t, _ in p]
    res = run_worker(sentences, verify, device)
    voice, keys = res["voice"], res["clips"]
    clips_dir = TTS_CACHE / "clips"
    drinks_dir = TTS_CACHE / "drinks"
    drinks_dir.mkdir(parents=True, exist_ok=True)
    files: dict[str, bytes] = {}
    encoded, seconds = 0, 0.0
    for rid, parts in scripts.items():
        ks = [keys[t] for t, _ in parts]
        breaks = [b for _, b in parts]
        dk = drink_key(ks, breaks, voice)
        ogg_path = drinks_dir / f"{dk}.opus"
        if not ogg_path.exists():
            pcm = [_silence(voice["lead_in"])]
            for i, (k, b) in enumerate(zip(ks, breaks)):
                pcm.append(_read_pcm(clips_dir / f"{k}.wav"))
                if i < len(ks) - 1:
                    pcm.append(_silence(voice["pause_section"] if b else voice["pause_sentence"]))
            pcm.append(_silence(voice["tail"]))
            encode(opusenc, b"".join(pcm), voice["opus_kbps"], ogg_path)
            encoded += 1
        opk, dur = to_opk(ogg_path.read_bytes())
        files[f"speech/{rid}.opk"] = opk
        seconds += dur
    total = sum(len(b) for b in files.values())
    print(f"speech: {len(files)} drinks, {len(set(sentences))} sentence clips "
          f"({res['generated']} recorded now{' on ' + res['device'].upper() if res['device'] else ''}, "
          f"{res['retried']} re-recorded by --verify), "
          f"{encoded} drinks re-encoded; {seconds / 60:.1f} min of audio, {total / 1024 / 1024:.2f} MB "
          f"at {voice['opus_kbps']} kbps; {time.time() - t0:.0f} s", flush=True)
    if verify:
        fails = res["verify_failed"]
        print(f"verify: {len(fails)} sentence(s) still above WER {voice['verify_wer']}"
              + (":" if fails else " (clean)"))
        for f in fails:
            print(f"  WER {f['wer']:.2f}: '{f['text']}'\n            heard '{f['heard']}'")
    return files


def say(drink_id: str, data_dir: Path) -> int:
    """Play a drink's clip on the Mac (from the cache; build the pack first)."""
    recipes = {r["id"]: r for r in json.loads((data_dir / "recipes.json").read_text())["recipes"]}
    if drink_id not in recipes:
        raise SystemExit(f"say: unknown drink id {drink_id!r}")
    result = TTS_CACHE / "result.json"
    if not result.exists():
        raise SystemExit("say: no clips yet; run build.py pack first")
    res = json.loads(result.read_text())
    parts = speech.parts(recipes[drink_id])
    try:
        ks = [res["clips"][t] for t, _ in parts]
    except KeyError:
        raise SystemExit(f"say: {drink_id} changed since the last pack; run build.py pack first") from None
    ogg = TTS_CACHE / "drinks" / f"{drink_key(ks, [b for _, b in parts], res['voice'])}.opus"
    if not ogg.exists():
        raise SystemExit(f"say: no clip for {drink_id}; run build.py pack first")
    need_opusenc()
    opusdec = shutil.which("opusdec")
    print("\n".join(t for t, _ in parts))
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td) / f"{drink_id}.wav"
        subprocess.run([opusdec, "--quiet", "--rate", str(RATE), str(ogg), str(wav)], check=True)
        return subprocess.run(["afplay", str(wav)]).returncode

