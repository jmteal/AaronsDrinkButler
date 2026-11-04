#!/usr/bin/env python3
"""
Run the Drink Butler firmware (emulator build) in Espressif's QEMU for the ESP32-S3.

Standard library only. tools/emulator/run.sh builds the firmware and calls this; see
firmware/EMULATOR.md.

  emu.py image  --build DIR --pack FILE [--fresh]
        Write DIR/qemu_flash.bin (16 MB): bootloader, partition table and app from
        DIR/flash_args, and the recipe pack at the "drinks" partition. An existing image
        is updated in place, so NVS (favorites, settings) survives rebuilds; --fresh
        starts from erased flash.

  emu.py run    --build DIR [--port 8765] [--no-browser] [--test OUTDIR]
        Start QEMU with no window (-display none) and serve a viewer at
        http://127.0.0.1:PORT: the screen (QMP screendumps) with mouse input, which is
        sent to the firmware's debug console as down/move/up. The serial log goes to
        this terminal; lines typed here go to the console (try "help").
        --test boots, saves OUTDIR/splash.png, taps, saves OUTDIR/home.png, checks
        "stats", then quits. Exit status 0 only if all of that worked.
"""
import argparse
import collections
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FLASH_SIZE = 16 * 1024 * 1024
LCD_W, LCD_H = 320, 480
HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.normpath(os.path.join(HERE, "..", "..", "firmware"))
CRASH_RE = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|assert failed|Stack smashing")


def log(msg):
    print(f"\033[36m[emu] {msg}\033[0m", file=sys.stderr, flush=True)


# ---------------------------------------------------------------- flash image

def drinks_offset():
    with open(os.path.join(FIRMWARE, "partitions.csv")) as f:
        for line in f:
            cols = [c.strip() for c in line.split("#")[0].split(",")]
            if cols and cols[0] == "drinks":
                return int(cols[3], 0), int(cols[4], 0)
    raise SystemExit("no 'drinks' partition in partitions.csv")


def cmd_image(a):
    img = os.path.join(a.build, "qemu_flash.bin")
    if a.fresh or not os.path.exists(img) or os.path.getsize(img) != FLASH_SIZE:
        log(f"new flash image (erased): {img}")
        with open(img, "wb") as f:
            f.write(b"\xff" * FLASH_SIZE)
    parts = []
    with open(os.path.join(a.build, "flash_args")) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("--"):
                off, path = line.split(None, 1)
                parts.append((int(off, 0), os.path.join(a.build, path)))
    off, size = drinks_offset()
    if os.path.getsize(a.pack) > size:
        raise SystemExit(f"{a.pack} is larger than the drinks partition")
    parts.append((off, a.pack))
    with open(img, "r+b") as f:
        for off, path in parts:
            with open(path, "rb") as src:
                data = src.read()
            f.seek(off)
            f.write(data)
            log(f"  0x{off:06x}  {os.path.relpath(path, a.build)}  ({len(data) / 1024:.0f} KB)")


def efuse_image():
    # The same eFuse contents `idf.py qemu` uses (ESP32-S3 chip revision 0.3)
    idf = os.environ.get("IDF_PATH", os.path.expanduser("~/esp/esp-idf"))
    sys.path[:0] = [os.path.join(idf, "tools", "idf_py_actions"), os.path.join(idf, "tools")]
    import qemu_ext  # noqa: E402  (needs IDF's Python env: run.sh sources export.sh)
    return qemu_ext.QEMU_TARGETS["esp32s3"].default_efuse


# ---------------------------------------------------------------- PNG / PPM

def png(w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6\n<w> <h>\n255\n<rgb>
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos)
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos:pos + w * h * 3]


# ---------------------------------------------------------------- QEMU

def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def connect(port, proc, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            raise SystemExit(f"QEMU exited (status {proc.returncode}) before it was ready")
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            time.sleep(0.1)
    raise SystemExit(f"timed out connecting to QEMU on port {port}")


class Qmp:
    def __init__(self, sock):
        sock.settimeout(10)
        self.f = sock.makefile("rwb")
        self.lock = threading.Lock()
        self._read()                    # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise EOFError("QMP closed")
            msg = json.loads(line)
            if "event" not in msg:
                return msg

    def cmd(self, name, **args):
        with self.lock:
            self.f.write(json.dumps({"execute": name, "arguments": args}).encode() + b"\n")
            self.f.flush()
            reply = self._read()
        if "error" in reply:
            raise RuntimeError(f"QMP {name}: {reply['error'].get('desc')}")
        return reply.get("return")


class Emulator:
    def __init__(self, build, psram="8M"):
        self.build = build
        self.qdir = os.path.join(build, "qemu")
        os.makedirs(self.qdir, exist_ok=True)
        efuse = os.path.join(build, "qemu_efuse.bin")
        if not os.path.exists(efuse):
            with open(efuse, "wb") as f:
                f.write(efuse_image())
        flash = os.path.join(build, "qemu_flash.bin")
        if not os.path.exists(flash):
            raise SystemExit(f"{flash} missing: run tools/emulator/run.sh (it builds the image)")
        self.qmp_port, self.ser_port = free_port(), free_port()
        argv = [
            "qemu-system-xtensa", "-M", "esp32s3", "-m", psram,
            "-drive", f"file={flash},if=mtd,format=raw",
            "-drive", f"file={efuse},if=none,format=raw,id=efuse",
            "-global", "driver=nvram.esp32s3.efuse,property=drive,value=efuse",
            "-global", "driver=timer.esp32s3.timg,property=wdt_disable,value=true",
            "-global", "driver=ssi_psram,property=is_octal,value=true",
            "-nic", "none", "-display", "none", "-monitor", "none",
            "-qmp", f"tcp:127.0.0.1:{self.qmp_port},server=on,wait=off",
            "-serial", f"tcp:127.0.0.1:{self.ser_port},server=on,wait=off",
            "-S",                       # paused until the serial port is connected
        ]
        log("starting QEMU (no window): " + " ".join(argv[:4]) + " ...")
        self.proc = subprocess.Popen(argv, stdin=subprocess.DEVNULL, start_new_session=True)
        self.qmp = Qmp(connect(self.qmp_port, self.proc))
        self.ser = connect(self.ser_port, self.proc)
        self.ser.settimeout(None)
        self.ser_lock = threading.Lock()
        self.lines = collections.deque(maxlen=2000)
        self.line_no = 0                # number of the next line
        self.cv = threading.Condition()
        self.crashed = None
        self.echo = True
        self.shot_lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()
        self.qmp.cmd("cont")

    def _reader(self):
        buf = b""
        while True:
            try:
                data = self.ser.recv(4096)
            except OSError:
                data = b""
            if not data:
                break
            buf += data
            *done, buf = buf.split(b"\n")
            for raw in done:
                line = raw.rstrip(b"\r").decode("utf-8", "replace")
                if self.echo:
                    print(line, flush=True)
                with self.cv:
                    self.lines.append((self.line_no, line))
                    self.line_no += 1
                    if CRASH_RE.search(line) and not self.crashed:
                        self.crashed = line
                    self.cv.notify_all()
        with self.cv:
            self.crashed = self.crashed or "serial port closed (QEMU exited)"
            self.cv.notify_all()

    def send(self, line):
        with self.ser_lock:
            self.ser.sendall(line.encode() + b"\n")

    def wait_for(self, pattern, timeout, since=0):
        rx = re.compile(pattern)
        end = time.time() + timeout
        with self.cv:
            while True:
                for n, line in self.lines:
                    if n >= since and rx.search(line):
                        return line
                if self.crashed:
                    raise RuntimeError(f"firmware crashed: {self.crashed}")
                left = end - time.time()
                if left <= 0:
                    raise TimeoutError(f"no '{pattern}' within {timeout} s")
                self.cv.wait(left)

    def screen(self):
        """(w, h, rgb bytes) of the virtual panel, via QMP screendump (PPM)."""
        with self.shot_lock:
            path = os.path.join(self.qdir, "screen.ppm")
            self.qmp.cmd("screendump", filename=path)
            return read_ppm(path)

    def screenshot(self, out):
        w, h, rgb = self.screen()
        with open(out, "wb") as f:
            f.write(png(w, h, rgb))
        return w, h, rgb

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.qmp.cmd("quit")
            except Exception:
                pass
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


# ---------------------------------------------------------------- pointer input

class Pointer:
    """Ordered pointer events to the console. Holds a press at least HOLD_S so a quick
    click is never missed by LVGL's 30 ms input poll."""
    HOLD_S = 0.08

    def __init__(self, emu):
        self.emu = emu
        self.q = collections.deque()
        self.cv = threading.Condition()
        self.down_at = 0.0
        threading.Thread(target=self._run, daemon=True).start()

    def push(self, kind, x, y):
        x = max(0, min(LCD_W - 1, int(x)))
        y = max(0, min(LCD_H - 1, int(y)))
        with self.cv:
            if kind == "move" and self.q and self.q[-1][0] == "move":
                self.q[-1] = (kind, x, y)       # coalesce
            else:
                self.q.append((kind, x, y))
            self.cv.notify()

    def _run(self):
        while True:
            with self.cv:
                while not self.q:
                    self.cv.wait()
                kind, x, y = self.q.popleft()
            if kind == "down":
                self.down_at = time.time()
            elif kind == "up":
                time.sleep(max(0.0, self.down_at + self.HOLD_S - time.time()))
            self.emu.send(f"{kind} {x} {y}")


# ---------------------------------------------------------------- viewer

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Drink Butler Emulator</title>
<style>
:root { --bg:#16110d; --panel:#231b15; --fg:#f2e6d8; --dim:#a8957f; --accent:#d9a35b; }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:14px -apple-system, system-ui, sans-serif;
       display:flex; gap:20px; padding:16px; flex-wrap:wrap; justify-content:center; }
.device { background:#000; padding:14px; border-radius:22px; box-shadow:0 8px 30px #0009; align-self:flex-start; }
#screen { display:block; width:calc(320px * var(--zoom, 1.5)); height:calc(480px * var(--zoom, 1.5));
          image-rendering:pixelated; cursor:pointer; touch-action:none; user-select:none; -webkit-user-drag:none; }
.side { width:min(460px, 100%); display:flex; flex-direction:column; gap:10px; }
h1 { font-size:16px; margin:0; color:var(--accent); }
.row { display:flex; gap:6px; flex-wrap:wrap; align-items:center; }
button, input { background:var(--panel); color:var(--fg); border:1px solid #4a3a2c; border-radius:6px; padding:5px 9px; font:inherit; }
button:hover { border-color:var(--accent); }
input { flex:1; min-width:0; font-family:ui-monospace, Menlo, monospace; }
#log { background:#0d0a08; border:1px solid #3a2d22; border-radius:6px; height:min(56vh, 520px); overflow:auto;
       font:11.5px/1.35 ui-monospace, Menlo, monospace; padding:6px 8px; white-space:pre-wrap; margin:0; }
.hint { color:var(--dim); font-size:12px; }
#status { color:var(--dim); font-size:12px; }
</style></head><body>
<div class="device"><img id="screen" alt="emulated 320x480 screen" draggable="false"></div>
<div class="side">
  <h1>Aaron's Drink Butler &middot; QEMU ESP32-S3</h1>
  <div class="hint">Click = tap, drag = swipe, scroll wheel = scroll. The firmware sees these as its touch screen.</div>
  <div class="row">
    <span class="hint">Zoom</span>
    <button data-z="1">1&times;</button><button data-z="1.5">1.5&times;</button><button data-z="2">2&times;</button>
    <button id="save">Save PNG</button>
    <span id="status"></span>
  </div>
  <form class="row" id="cmd"><input id="cmdline" placeholder="console command, e.g. help, stats, tab browse, open negroni" autocomplete="off"><button>Send</button></form>
  <pre id="log"></pre>
</div>
<script>
const img = document.getElementById('screen'), logEl = document.getElementById('log'), st = document.getElementById('status');
let zoom = 1.5; try { zoom = parseFloat(localStorage.getItem('zoom')) || 1.5; } catch (e) {}
const setZoom = z => { zoom = z; document.documentElement.style.setProperty('--zoom', z); try { localStorage.setItem('zoom', z); } catch (e) {} };
setZoom(zoom);
document.querySelectorAll('[data-z]').forEach(b => b.onclick = () => setZoom(parseFloat(b.dataset.z)));
document.getElementById('save').onclick = () => { const a = document.createElement('a'); a.href = '/frame.png?dl=' + Date.now(); a.download = 'drink-butler.png'; a.click(); };

let etag = '', frames = 0, t0 = performance.now();
async function poll() {
  for (;;) {
    try {
      const r = await fetch('/frame.png', { headers: etag ? { 'If-None-Match': etag } : {} });
      if (r.status === 200) {
        etag = r.headers.get('ETag') || '';
        const url = URL.createObjectURL(await r.blob());
        const old = img.src; img.src = url; if (old.startsWith('blob:')) URL.revokeObjectURL(old);
        frames++;
      } else if (r.status !== 304) { st.textContent = 'emulator stopped'; await new Promise(s => setTimeout(s, 1000)); continue; }
      const dt = (performance.now() - t0) / 1000;
      if (dt > 2) { st.textContent = (frames / dt).toFixed(1) + ' new frames/s'; frames = 0; t0 = performance.now(); }
    } catch (e) { st.textContent = 'emulator not reachable'; await new Promise(s => setTimeout(s, 1000)); }
    await new Promise(s => setTimeout(s, 40));
  }
}
poll();

const post = (path, body) => fetch(path, { method: 'POST', body }).catch(() => {});
const pt = e => { const b = img.getBoundingClientRect(); return [Math.round((e.clientX - b.left) / zoom), Math.round((e.clientY - b.top) / zoom)]; };
let down = false, lastMove = 0;
img.addEventListener('pointerdown', e => { e.preventDefault(); img.setPointerCapture(e.pointerId); down = true; post('/input', 'down ' + pt(e).join(' ')); });
img.addEventListener('pointermove', e => { if (!down) return; const n = performance.now(); if (n - lastMove < 30) return; lastMove = n; post('/input', 'move ' + pt(e).join(' ')); });
const up = e => { if (!down) return; down = false; post('/input', 'up ' + pt(e).join(' ')); };
img.addEventListener('pointerup', up); img.addEventListener('pointercancel', up);
let wheelBusy = false;
img.addEventListener('wheel', e => {
  e.preventDefault(); if (wheelBusy || down) return; wheelBusy = true;
  const [x, y] = pt(e), d = Math.max(-160, Math.min(160, -e.deltaY));
  post('/input', 'wheel ' + x + ' ' + y + ' ' + Math.round(d)).finally(() => setTimeout(() => wheelBusy = false, 120));
}, { passive: false });

document.getElementById('cmd').onsubmit = e => { e.preventDefault(); const c = document.getElementById('cmdline'); if (c.value.trim()) post('/console', c.value.trim()); c.value = ''; };

let next = 0;
async function pollLog() {
  for (;;) {
    try {
      const r = await (await fetch('/log?after=' + next)).json();
      if (r.lines.length) {
        const stick = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 20;
        logEl.textContent += r.lines.join('\\n') + '\\n';
        if (logEl.textContent.length > 200000) logEl.textContent = logEl.textContent.slice(-150000);
        if (stick) logEl.scrollTop = logEl.scrollHeight;
      }
      next = r.next;
    } catch (e) {}
    await new Promise(s => setTimeout(s, 300));
  }
}
pollLog();
</script></body></html>
"""


def serve(emu, port):
    ptr = Pointer(emu)
    cache = {"key": None, "png": b"", "etag": ""}
    cache_lock = threading.Lock()

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, body=b"", ctype="text/plain", extra=None):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if body:
                self.wfile.write(body)

        def do_GET(self):
            path, _, query = self.path.partition("?")
            if path == "/":
                return self._send(200, PAGE.encode(), "text/html; charset=utf-8")
            if path == "/frame.png":
                if emu.proc.poll() is not None:
                    return self._send(503, b"QEMU is not running")
                try:
                    w, h, rgb = emu.screen()
                except Exception as e:
                    return self._send(503, str(e).encode())
                key = zlib.crc32(rgb)
                with cache_lock:
                    if key != cache["key"]:
                        cache.update(key=key, png=png(w, h, rgb), etag=f'"{key:08x}"')
                    body, etag = cache["png"], cache["etag"]
                if self.headers.get("If-None-Match") == etag and "dl=" not in query:
                    return self._send(304, extra={"ETag": etag})
                return self._send(200, body, "image/png", {"ETag": etag})
            if path == "/log":
                after = int(dict(p.split("=", 1) for p in query.split("&") if "=" in p).get("after", 0))
                with emu.cv:
                    lines = [l for n, l in emu.lines if n >= after]
                    nxt = emu.line_no
                return self._send(200, json.dumps({"next": nxt, "lines": lines}).encode(), "application/json")
            return self._send(404)

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode(errors="replace").strip()
            if self.path == "/input":
                parts = body.split()
                try:
                    if parts[0] in ("down", "move", "up"):
                        ptr.push(parts[0], float(parts[1]), float(parts[2]))
                    elif parts[0] == "wheel":   # one short swipe per wheel notch
                        x, y, d = int(parts[1]), int(parts[2]), int(parts[3])
                        y1 = max(20, min(LCD_H - 20, y))
                        y2 = max(20, min(LCD_H - 20, y1 + d))
                        if y1 != y2:
                            emu.send(f"drag {x} {y1} {x} {y2} 120")
                except (IndexError, ValueError):
                    return self._send(400)
                return self._send(204)
            if self.path == "/console":
                if body:
                    emu.send(body)
                return self._send(204)
            return self._send(404)

    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


# ---------------------------------------------------------------- run / test

def distinct_colors(rgb, step=97):
    return len({rgb[i:i + 3] for i in range(0, len(rgb) - 3, 3 * step)})


def run_test(emu, outdir):
    os.makedirs(outdir, exist_ok=True)
    t0 = time.time()
    line = emu.wait_for(r"UI up \d+ ms after boot", 180)
    log(f"booted in {time.time() - t0:.1f} s (wall clock): {line.strip()}")
    time.sleep(2.0)
    w, h, rgb = emu.screenshot(os.path.join(outdir, "splash.png"))
    splash_colors = distinct_colors(rgb)
    log(f"splash.png {w}x{h}, {splash_colors} distinct colours sampled")
    if (w, h) != (LCD_W, LCD_H) or splash_colors < 3:
        raise RuntimeError("the splash screen is blank or the wrong size")
    mark = emu.line_no
    emu.send("tap 160 300")             # the splash waits for a tap
    emu.wait_for(r"^OK$", 20, since=mark)
    time.sleep(3.0)
    w, h, rgb2 = emu.screenshot(os.path.join(outdir, "home.png"))
    log(f"home.png {w}x{h}, {distinct_colors(rgb2)} distinct colours sampled")
    if rgb2 == rgb:
        raise RuntimeError("the screen did not change after tapping the splash")
    mark = emu.line_no
    emu.send("stats")
    log("console: " + emu.wait_for(r"^STATS drinks=", 20, since=mark).strip())
    if emu.crashed:
        raise RuntimeError(f"firmware crashed: {emu.crashed}")
    log(f"PASS: splash and Home rendered, console works ({outdir})")


def cmd_run(a):
    emu = Emulator(a.build, a.psram)
    stop = threading.Event()

    def on_signal(*_):
        stop.set()
    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    try:
        if a.test:
            emu.echo = a.verbose
            try:
                run_test(emu, a.test)
                return 0
            except Exception as e:
                log(f"FAIL: {e}")
                with emu.cv:
                    tail = [l for _, l in emu.lines][-40:]
                print("\n".join(tail), file=sys.stderr)
                return 1
        srv = serve(emu, a.port)
        url = f"http://127.0.0.1:{srv.server_address[1]}/"
        log(f"viewer: {url}   (Ctrl-C here stops the emulator)")
        if not a.no_browser:
            webbrowser.open(url)
        if sys.stdin.isatty():
            def stdin_loop():
                for line in sys.stdin:
                    if line.strip():
                        emu.send(line.strip())
            threading.Thread(target=stdin_loop, daemon=True).start()
        while not stop.is_set() and emu.proc.poll() is None:
            stop.wait(0.5)
        if emu.proc.poll() is not None:
            log(f"QEMU exited with status {emu.proc.returncode}")
        if emu.crashed:
            log(f"last problem seen on serial: {emu.crashed}")
        return 0
    finally:
        emu.stop()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("image")
    i.add_argument("--build", required=True)
    i.add_argument("--pack", required=True)
    i.add_argument("--fresh", action="store_true")
    r = sub.add_parser("run")
    r.add_argument("--build", required=True)
    r.add_argument("--port", type=int, default=8765)
    r.add_argument("--psram", default="8M", help="PSRAM size given to QEMU (the board has 8 MB)")
    r.add_argument("--no-browser", action="store_true")
    r.add_argument("--test", metavar="OUTDIR")
    r.add_argument("--verbose", action="store_true", help="with --test, print the serial log")
    a = p.parse_args()
    a.build = os.path.abspath(a.build)
    if a.cmd == "image":
        cmd_image(a)
        return 0
    return cmd_run(a)


if __name__ == "__main__":
    sys.exit(main())
