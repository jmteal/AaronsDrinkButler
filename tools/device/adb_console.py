#!/usr/bin/env python3
"""Drive the firmware's serial debug console (CONFIG_ADB_DEBUG_CONSOLE) from the Mac.

    python tools/device/adb_console.py stats
    python tools/device/adb_console.py "tab browse" "shot browse.png" "open negroni" "shot detail.png"
    python tools/device/adb_console.py --log 5            # just print the log for 5 s

Each argument is one console command (see firmware/main/app_console.c); "shot FILE.png"
saves a screenshot. Needs pyserial (the ESP-IDF Python environment has it:
`. ~/esp/esp-idf/export.sh` first). The port is opened without toggling DTR/RTS, so the
board is not reset (but on macOS opening the port resets it regardless; the script then
waits for the console and the splash). --reset forces a restart.
"""

from __future__ import annotations

import argparse
import base64
import glob
import re
import struct
import sys
import time
import zlib

import serial


B64_LINE = re.compile(r"[A-Za-z0-9+/]{1,76}={0,2}")


def png_from_rgb565(data: bytes, w: int, h: int) -> bytes:
    rows = []
    for y in range(h):
        row = bytearray([0])
        for (v,) in struct.iter_unpack("<H", data[y * w * 2:(y + 1) * w * 2]):
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        rows.append(bytes(row))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))


class Console:
    def __init__(self, port: str):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.2
        self.s.dtr = False   # USB-Serial/JTAG: DTR/RTS toggles reset the chip
        self.s.rts = False
        self.s.open()
        # On macOS opening the port resets the board anyway. If it does, wait for the
        # console and for the startup splash (2.5 s) so taps land on the real UI.
        for line in self.lines(6):
            if "debug console ready" in line:
                time.sleep(3)
                break

    def lines(self, timeout: float):
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            buf += self.s.read(4096)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                yield line.decode(errors="replace").rstrip("\r")

    def reset(self) -> None:
        self.s.rts = True
        time.sleep(0.1)
        self.s.rts = False
        for line in self.lines(15):
            print(line)
            if "debug console ready" in line:
                time.sleep(3)
                return
        raise SystemExit("board did not report the console within 15 s")

    def command(self, cmd: str, timeout: float = 10) -> list[str]:
        self.s.reset_input_buffer()
        self.s.write((cmd + "\n").encode())
        out = []
        for line in self.lines(timeout):
            out.append(line)
            if line in ("OK",) or line.startswith("ERR"):
                return out
        raise SystemExit(f"no reply to {cmd!r}: {out[-5:]}")

    def shot(self, path: str) -> None:
        self.s.reset_input_buffer()
        self.s.write(b"shot\n")
        w = h = size = None
        b64 = []
        for line in self.lines(30):
            if line.startswith("ERR"):
                raise SystemExit(line)
            if line.startswith("SHOT "):
                w, h, size = map(int, line.split()[1:4])
            elif line == "SHOT-END":
                break
            elif w is not None and B64_LINE.fullmatch(line):   # skips any log line that slips in
                b64.append(line)
        else:
            raise SystemExit("screenshot timed out")
        data = base64.b64decode("".join(b64))
        if len(data) != size:
            raise SystemExit(f"screenshot: got {len(data)} of {size} bytes")
        with open(path, "wb") as f:
            f.write(png_from_rgb565(data, w, h))
        print(f"saved {path} ({w}x{h})")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*")
    ap.add_argument("-p", "--port", default=None, help="default: the first /dev/cu.usbmodem*")
    ap.add_argument("--reset", action="store_true", help="restart the board first")
    ap.add_argument("--log", type=float, default=0, help="print the serial log for N seconds at the end")
    args = ap.parse_args()
    port = args.port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)
    if not port:
        raise SystemExit("no /dev/cu.usbmodem* port; is the board plugged in?")
    c = Console(port)
    if args.reset:
        c.reset()
    for cmd in args.commands:
        if cmd.startswith("shot "):
            c.shot(cmd.split(None, 1)[1])
        elif cmd.startswith("sleep "):
            time.sleep(float(cmd.split()[1]))
        else:
            for line in c.command(cmd):
                print(line)
    for line in c.lines(args.log):
        print(line)


if __name__ == "__main__":
    sys.exit(main())
