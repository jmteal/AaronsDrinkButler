"""Sentence clips for "speak the recipe", run inside tools/data/.venv-tts (torch, Parler-TTS).

build.py pack starts this with a job file and reads back a result file; nothing else uses it.

    tts_worker.py JOB.json RESULT.json

Job:    {"sentences": [...], "clips_dir": "...", "voice": "voice.yaml", "verify": false,
         "device": "auto"}
Result: {"device": "mps", "voice": {...}, "clips": {sentence: key}, "generated": n,
         "verify_failed": [{"text", "heard", "wer"}], ...}

Each sentence becomes clips_dir/<key>.wav (16 kHz mono, 16-bit) plus <key>.json (seed,
word error rate when verified). The key is a hash of the sentence, the voice settings that
change the sound, the model revision and the Parler-TTS commit, so a clip is only recorded
again when one of those changes.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
import unicodedata
import wave
from pathlib import Path

import numpy as np
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adb import speech  # noqa: E402

MODEL = "parler-tts/parler-tts-mini-v1"
MODEL_REVISION = "0392b9451a601e528fd863bbb0598431fee810d9"
PARLER_COMMIT = "d108732cd57788ec86bc857d99a6cabd66663d68"   # requirements-tts.lock
WHISPER_REVISION = "main"
OUT_RATE = 16000
SOUND_KEYS = ("speaker", "description", "seed")


def log(msg: str) -> None:
    print(msg, flush=True)


def load_voice(path: Path) -> dict:
    v = yaml.safe_load(path.read_text())
    v["description"] = " ".join(v["description"].split()).replace("{speaker}", v["speaker"])
    return v


def voice_hash(v: dict) -> str:
    ident = {k: v[k] for k in SOUND_KEYS}
    ident.update(model=MODEL, model_revision=MODEL_REVISION, parler=PARLER_COMMIT, rate=OUT_RATE)
    return hashlib.sha256(json.dumps(ident, sort_keys=True).encode()).hexdigest()


def clip_key(vh: str, text: str) -> str:
    return hashlib.sha256(f"{vh}\n{text}".encode()).hexdigest()[:24]


def write_wav(path: Path, pcm: np.ndarray) -> None:
    tmp = path.with_suffix(".tmp")
    with wave.open(str(tmp), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(pcm.astype("<i2").tobytes())
    os.replace(tmp, path)


def read_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")


class Tts:
    def __init__(self, voice: dict, want: str):
        self.voice = voice
        self.want = want
        self.model = None
        self.device = None

    def _load(self):
        import contextlib
        import io

        import torch
        from transformers import AutoTokenizer
        from transformers.utils import logging as hf_logging

        hf_logging.set_verbosity_error()
        with contextlib.redirect_stdout(io.StringIO()):   # parler prints its whole config on import/load
            from parler_tts import ParlerTTSForConditionalGeneration

        t = time.time()
        log(f"tts: loading {MODEL}@{MODEL_REVISION[:8]} (first run downloads about 3.5 GB to the HF cache)")
        self.tok = AutoTokenizer.from_pretrained(MODEL, revision=MODEL_REVISION)
        with contextlib.redirect_stdout(io.StringIO()):
            model = ParlerTTSForConditionalGeneration.from_pretrained(MODEL, revision=MODEL_REVISION)
        # auto = CPU: MPS runs this model, but measured on an M3 it is about twice as slow
        # (one sentence at a time, autoregressive). --device mps to use it anyway.
        devices = ["cpu"] if self.want == "auto" else [self.want]
        if "mps" in devices and not torch.backends.mps.is_available():
            devices.remove("mps")
        for dev in devices:
            try:
                self.model = model.to(dev)
                self.device = dev
                self._generate("Testing.", self.voice["seed"])   # smoke test: some ops fail late on MPS
                break
            except Exception as e:  # noqa: BLE001
                log(f"tts: {dev} failed ({type(e).__name__}: {e}); trying the next device")
                self.model = None
        if self.model is None:
            raise SystemExit("tts: no working device")
        self.desc = self.tok(self.voice["description"], return_tensors="pt").to(self.device)
        self.rate = model.config.sampling_rate
        log(f"tts: model ready on {self.device.upper()} in {time.time() - t:.0f} s")

    def _generate(self, text: str, seed: int) -> np.ndarray:
        import torch
        from transformers import set_seed

        if not hasattr(self, "desc"):
            desc = self.tok(self.voice["description"], return_tensors="pt").to(self.device)
        else:
            desc = self.desc
        p = self.tok(text, return_tensors="pt").to(self.device)
        set_seed(seed)
        with torch.no_grad():
            g = self.model.generate(input_ids=desc.input_ids, attention_mask=desc.attention_mask,
                                    prompt_input_ids=p.input_ids, prompt_attention_mask=p.attention_mask)
        return g.cpu().float().numpy().squeeze()

    def say(self, text: str, seed: int) -> np.ndarray:
        """16 kHz int16 PCM for one sentence, edge silence trimmed."""
        from scipy.signal import resample_poly

        if self.model is None:
            self._load()
        a = self._generate(text, seed)
        g = np.gcd(OUT_RATE, self.rate)
        a = resample_poly(a, OUT_RATE // g, self.rate // g)
        a = trim(a)
        peak = float(np.max(np.abs(a))) if a.size else 0.0
        if peak > 0:
            a = a * (0.89 / peak)          # -1 dBFS; the device volume sets the loudness
        return np.clip(a * 32767, -32768, 32767).astype(np.int16)


def trim(a: np.ndarray, thresh: float = 0.01, pad: int = OUT_RATE // 50) -> np.ndarray:
    idx = np.flatnonzero(np.abs(a) > thresh * max(1e-9, float(np.max(np.abs(a)))))
    if idx.size == 0:
        return a
    return a[max(0, idx[0] - pad): idx[-1] + pad]


# ---- verification

def norm_words(s: str) -> list[str]:
    s = unicodedata.normalize("NFKD", s.lower())
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.replace("¼", " 1/4").replace("½", " 1/2").replace("¾", " 3/4")
    s = re.sub(r"(\d+)\s*/\s*4", lambda m: {"1": "a quarter", "2": "a half", "3": "three quarters"}.get(m.group(1), m.group(0)), s)
    s = re.sub(r"(\d+)\s*/\s*2", "a half", s)
    s = re.sub(r"\d+", lambda m: speech.words(int(m.group())), s)
    s = re.sub(r"[^a-z' ]+", " ", s.replace("-", " "))
    return s.split()


def wer(ref: str, hyp: str) -> float:
    r, h = norm_words(ref), norm_words(hyp)
    if not r:
        return 0.0
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = min(d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] != h[j - 1]))
            prev, d[j] = d[j], cur
    return d[len(h)] / len(r)


class Whisper:
    def __init__(self, size: str):
        self.size = size
        self.model = None

    def heard(self, pcm: np.ndarray) -> str:
        if self.model is None:
            from faster_whisper import WhisperModel

            log(f"verify: loading faster-whisper {self.size} (CPU, int8)")
            self.model = WhisperModel(self.size, device="cpu", compute_type="int8")
        segs, _ = self.model.transcribe(pcm.astype(np.float32) / 32768, language="en", beam_size=5,
                                        condition_on_previous_text=False, vad_filter=False)
        return " ".join(s.text.strip() for s in segs)


def fmt_eta(s: float) -> str:
    s = int(s)
    return f"{s // 3600}h{s % 3600 // 60:02d}m" if s >= 3600 else f"{s // 60}m{s % 60:02d}s"


def main(job_path: str, result_path: str) -> int:
    job = json.loads(Path(job_path).read_text())
    voice = load_voice(Path(job["voice"]))
    vh = voice_hash(voice)
    clips_dir = Path(job["clips_dir"])
    clips_dir.mkdir(parents=True, exist_ok=True)
    texts = list(dict.fromkeys(job["sentences"]))
    keys = {t: clip_key(vh, t) for t in texts}
    todo = [t for t in texts if not (clips_dir / f"{keys[t]}.wav").exists()]
    tts = Tts(voice, job.get("device", "auto"))
    verify = bool(job.get("verify"))
    whisper = Whisper(voice.get("verify_model", "small")) if verify else None
    max_wer = float(voice.get("verify_wer", 0.25))
    retries = int(voice.get("verify_retries", 3))
    log(f"tts: {len(texts)} sentences, {len(texts) - len(todo)} cached, {len(todo)} to record"
        f" (speaker {voice['speaker']}, seed {voice['seed']})")

    def record(text: str, seed: int) -> np.ndarray:
        return tts.say(text, seed)

    def save(text: str, pcm: np.ndarray, meta: dict) -> None:
        write_wav(clips_dir / f"{keys[text]}.wav", pcm)
        (clips_dir / f"{keys[text]}.json").write_text(json.dumps({"text": text, **meta}))

    t0, audio_s = time.time(), 0.0
    chars_total = sum(len(t) for t in todo) or 1
    chars_done = 0
    for n, text in enumerate(todo, 1):
        pcm = record(text, voice["seed"])
        meta = {"seed": voice["seed"]}
        if verify:
            meta.update(check(text, pcm, whisper))
        save(text, pcm, meta)
        audio_s += len(pcm) / OUT_RATE
        chars_done += len(text)
        el = time.time() - t0
        eta = el / chars_done * (chars_total - chars_done)
        if n == 1 or n % 10 == 0 or n == len(todo):
            log(f"tts: {n}/{len(todo)} recorded, {audio_s / 60:.1f} min of audio in {fmt_eta(el)}"
                f" ({audio_s / max(el, 1e-6):.2f}x real time), ETA {fmt_eta(eta)}")

    failed = []
    retried = 0
    if verify:
        t1 = time.time()
        unchecked = [t for t in texts if "wer" not in meta_of(clips_dir, keys[t])]
        if unchecked:
            log(f"verify: transcribing {len(unchecked)} clips")
        for n, text in enumerate(unchecked, 1):
            m = meta_of(clips_dir, keys[text])
            m.update(check(text, read_wav(clips_dir / f"{keys[text]}.wav"), whisper))
            (clips_dir / f"{keys[text]}.json").write_text(json.dumps(m))
            if n % 50 == 0 or n == len(unchecked):
                log(f"verify: {n}/{len(unchecked)} transcribed in {fmt_eta(time.time() - t1)}")
        for text in texts:
            m = meta_of(clips_dir, keys[text])
            if m["wer"] <= max_wer or m.get("retried"):
                continue
            best_pcm, best = None, m
            for attempt in range(1, retries + 1):
                seed = voice["seed"] + 1000 * attempt
                pcm = record(text, seed)
                c = check(text, pcm, whisper)
                log(f"verify: retry {attempt} seed {seed}: WER {c['wer']:.2f} '{text}' heard '{c['heard']}'")
                if c["wer"] < best["wer"]:
                    best_pcm, best = pcm, {"text": text, "seed": seed, **c}
                if c["wer"] <= max_wer:
                    break
            best["retried"] = True     # don't retry again on the next run
            if best_pcm is not None:
                save(text, best_pcm, {k: v for k, v in best.items() if k != "text"})
            else:
                (clips_dir / f"{keys[text]}.json").write_text(json.dumps(best))
            retried += 1
        for text in texts:
            m = meta_of(clips_dir, keys[text])
            if m["wer"] > max_wer:
                failed.append({"text": text, "heard": m["heard"], "wer": round(m["wer"], 3)})

    Path(result_path).write_text(json.dumps({
        "device": tts.device, "voice": voice, "voice_hash": vh, "clips": keys,
        "generated": len(todo), "retried": retried, "verify_failed": failed,
        "model": f"{MODEL}@{MODEL_REVISION}", "parler_commit": PARLER_COMMIT,
    }))
    return 0


def check(text: str, pcm: np.ndarray, whisper: Whisper) -> dict:
    heard = whisper.heard(pcm)
    return {"wer": wer(text, heard), "heard": heard}


def meta_of(clips_dir: Path, key: str) -> dict:
    p = clips_dir / f"{key}.json"
    return json.loads(p.read_text()) if p.exists() else {}


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
