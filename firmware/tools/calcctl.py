#!/usr/bin/env python3
"""Drive the calculator's debug console from the Mac (USB, hardware UART or simulator).

  calcctl.py version                       (finds the calculator on USB by its VID:PID)
  calcctl.py --port /dev/cu.usbmodemXXXX  "type \\"2+3\\n\\"" "state last_output"
  calcctl.py --shot screen.png
  calcctl.py --sim build/sim/calcsim      --shot screen.png "key F1"
  calcctl.py -i                            (interactive; Ctrl-D to quit)
  calcctl.py --upload-module build/modules/MANUA010.MOD   (install a content module)
  calcctl.py flash [--release DIR] [--core]   (restart into USB DFU, flash both banks)
  calcctl.py --list                        (the serial ports that are calculators)

Each argument is one console command. The client waits for the "OK"/"ERR ..."
terminator of each command (see README.md, "Console protocol"). --shot sends
`screen`, decodes the base64 RLE dump, checks the CRC32 and writes a PNG.
No third-party modules are needed (the serial port is driven with termios).
Exit status is 1 if any command answered ERR.

Transport: without --port and --sim the calculator's own USB serial port is
looked up by the USB VID:PID in include/usb_ids.h (macOS: ioreg; Linux:
sysfs); --port opens a serial device (the calculator's USB port, or the debug
probe's USB-UART at --baud) in raw mode; --sim starts calcsim with pipes (its
console is stdin/stdout). Lines starting with "# " are asynchronous log
messages (con_log) and are passed to stderr.

Module uploads over USB keep several `module put` lines in flight (--window,
8 on the calculator's USB port, 1 elsewhere): the console answers each with
OK, and USB flow control holds the computer back while the calculator
programs the QSPI, so nothing is lost.

`flash` sends the console command `dfu` (the calculator saves its session and
restarts into the STM32's ROM bootloader, no BOOT button), waits for the DFU
device, then runs dfu-util exactly as release/FLASHING.md does: bank 2 at
0x08100000 first, then bank 1 at 0x08000000 with :leave.
"""
import argparse
import base64
import glob
import os
import re
import select
import shutil
import struct
import subprocess
import sys
import termios
import time
import tty
import zlib

TOOLS = os.path.dirname(os.path.abspath(__file__))
DFU_ID = "0483:df11"  # the STM32 ROM bootloader's DFU device (AN2606)


def usb_ids():
    """(VID, PID) of the calculator from include/usb_ids.h, the one place they are defined."""
    vid, pid = 0x1209, 0x0001  # if the header is not next to this script (a copied calcctl.py)
    try:
        text = open(os.path.join(TOOLS, "..", "include", "usb_ids.h")).read()
        vid = int(re.search(r"#define\s+USB_VID\s+(0x[0-9A-Fa-f]+)", text).group(1), 16)
        pid = int(re.search(r"#define\s+USB_PID\s+(0x[0-9A-Fa-f]+)", text).group(1), 16)
    except (OSError, AttributeError):
        pass
    return vid, pid


def find_ports():
    """Serial devices of USB devices with the calculator's VID:PID."""
    vid, pid = usb_ids()
    found = []
    if sys.platform == "darwin":
        # every USB device with its subtree; the CDC port's IOCalloutDevice is inside it
        out = subprocess.run(["ioreg", "-r", "-c", "IOUSBHostDevice", "-l", "-w", "0"], capture_output=True,
                             text=True).stdout
        for block in re.split(r"\n(?=\+-o )", out):
            v = re.search(r'"idVendor" = (\d+)', block)
            p = re.search(r'"idProduct" = (\d+)', block)
            if v and p and int(v.group(1)) == vid and int(p.group(1)) == pid:
                found += re.findall(r'"IOCalloutDevice" = "([^"]+)"', block)
    else:
        for tty_dev in glob.glob("/sys/class/tty/ttyACM*"):
            d = os.path.realpath(os.path.join(tty_dev, "device", ".."))
            try:
                v = int(open(os.path.join(d, "idVendor")).read(), 16)
                p = int(open(os.path.join(d, "idProduct")).read(), 16)
            except OSError:
                continue
            if (v, p) == (vid, pid):
                found.append("/dev/" + os.path.basename(tty_dev))
    return sorted(set(found))


def find_port(timeout=0.0):
    """The calculator's serial port; waits up to timeout seconds for it to appear."""
    end = time.time() + timeout
    while True:
        ports = find_ports()
        if len(ports) == 1:
            return ports[0]
        if len(ports) > 1:
            raise SystemExit("several calculators are connected: choose one with --port " + " ".join(ports))
        if time.time() >= end:
            vid, pid = usb_ids()
            raise SystemExit(f"no calculator on USB ({vid:04x}:{pid:04x}); connect it, or use --port / --sim")
        time.sleep(0.5)


class SerialLink:
    """Console over a serial device, using termios directly (raw mode, the
    requested baud rate, modem control lines ignored via CLOCAL)."""

    def __init__(self, port, baud):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise SystemExit(f"unsupported baud {baud}")
        attrs[4] = attrs[5] = speed
        attrs[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b""

    def write(self, data):
        """All of data: the descriptor is non-blocking, and a USB port pushes
        back (flow control) while the calculator is busy."""
        while data:
            try:
                n = os.write(self.fd, data)
            except BlockingIOError:
                n = 0
            data = data[n:]
            if data:
                select.select([], [self.fd], [], 1.0)

    def readline(self, timeout):
        """Next line without its CR/LF; TimeoutError if none completes within
        `timeout` seconds."""
        end = time.time() + timeout
        while b"\n" not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("no response")
            r, _, _ = select.select([self.fd], [], [], left)
            if r:
                data = os.read(self.fd, 4096)
                if not data:
                    raise EOFError("the serial port closed (device gone)")
                self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def close(self):
        os.close(self.fd)


class SimLink:
    """Console of a calcsim child process over pipes. Piped input puts the
    simulator on its virtual clock (see platform/sim/sim_hal.c)."""

    def __init__(self, path, extra):
        self.p = subprocess.Popen([path] + extra, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.fd = self.p.stdout.fileno()
        self.buf = b""

    def write(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def readline(self, timeout):
        """Next line without its CR/LF; TimeoutError if none completes within
        `timeout` seconds, EOFError if the simulator exits."""
        end = time.time() + timeout
        while b"\n" not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("no response")
            r, _, _ = select.select([self.fd], [], [], left)
            if r:
                data = os.read(self.fd, 4096)
                if not data:
                    raise EOFError("simulator exited")
                self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")


def command(link, cmd, timeout=10.0, quiet=False):
    """Send one command; return (ok, lines).

    lines are the command's output lines before the terminator; on ERR the
    "ERR ..." line is appended. "# " log lines are printed to stderr (unless
    quiet) and not returned.
    """
    link.write((cmd + "\n").encode())
    return reply(link, timeout, quiet)


def reply(link, timeout=10.0, quiet=False):
    """The next command's output up to its terminator: (ok, lines) as command() returns."""
    lines = []
    while True:
        l = link.readline(timeout)
        if l.startswith("# "):
            if not quiet:
                print(l, file=sys.stderr)
            continue
        if l == "OK":
            return True, lines
        if l.startswith("ERR"):
            return False, lines + [l]
        lines.append(l)


UPLOAD_CHUNK = 176  # bytes per `module put` line: 352 hex digits, inside the 383-byte console line


def upload_module(link, path, force=False, timeout=30.0, window=1):
    """Install a content module (core/modstore.h) over the console: `module
    begin`, one `module put HEX` per 176 bytes, `module end`. At 115200 baud
    the hex doubles the size: ~25 s for the 126 KB manual; over USB, with
    `window` lines in flight, the QSPI programming is the limit (a second or
    two). The firmware checks the header first and verifies the payload
    before committing, so an interrupted upload leaves the previous version
    in use. Returns (ok, message)."""
    data = open(path, "rb").read()
    ok, lines = command(link, "module begin" + (" force" if force else ""), timeout)
    if not ok:
        return False, " ".join(lines)
    t0 = time.time()
    chunks = range(0, len(data), UPLOAD_CHUNK)
    inflight = []  # byte offsets of the lines sent and not answered yet
    for i in list(chunks) + [None]:
        if i is not None:
            link.write(("module put " + data[i:i + UPLOAD_CHUNK].hex() + "\n").encode())
            inflight.append(i)
        while inflight and (len(inflight) >= max(1, window) or i is None):
            at = inflight.pop(0)
            ok, lines = reply(link, timeout, quiet=True)
            if not ok:
                for _ in inflight:  # the lines already sent answer ERR too (no upload in progress)
                    reply(link, timeout, quiet=True)
                command(link, "module abort", timeout, quiet=True)
                return False, f"at byte {at}: " + " ".join(lines)
        if i is not None and sys.stderr.isatty() and (i // UPLOAD_CHUNK) % 32 == 0:
            print(f"\rupload {path}: {100 * i // len(data):3d} %", end="", file=sys.stderr)
    ok, lines = command(link, "module end", max(timeout, 60.0))
    if sys.stderr.isatty():
        print(file=sys.stderr)
    msg = " ".join(lines)
    return ok, f"{msg} ({len(data)} bytes in {time.time() - t0:.1f} s)" if ok else msg


def dfu_util(args, quiet=False):
    """Run dfu-util; (returncode, output)."""
    p = subprocess.run(["dfu-util"] + args, capture_output=True, text=True)
    out = p.stdout + p.stderr
    if not quiet:
        print(out.rstrip())
    return p.returncode, out


def flash(link, release, core=False, timeout=20.0):
    """Restart the calculator into the ROM USB DFU bootloader (console `dfu`)
    and flash the release images with dfu-util, bank 2 first and then bank 1
    with :leave (release/FLASHING.md). Returns 0 on success."""
    names = ("calc_core_bank2.bin", "calc_core.bin") if core else ("calc_bank2.bin", "calc.bin")
    bank2, bank1 = (os.path.join(release, n) for n in names)
    for f in (bank2, bank1):
        if not os.path.isfile(f):
            raise SystemExit(f"{f} not found (make release, or --release DIR)")
    if not shutil.which("dfu-util"):
        raise SystemExit("dfu-util not found: install it (MacPorts: sudo port install dfu-util; Homebrew: "
                         "brew install dfu-util)")
    print("restarting the calculator into its USB DFU bootloader ...")
    try:
        ok, lines = command(link, "dfu", 5.0)
        if not ok:
            raise SystemExit("dfu refused: " + " ".join(lines))
    except (TimeoutError, EOFError, OSError):
        pass  # the device may vanish before the OK arrives
    if hasattr(link, "close"):
        link.close()
    end = time.time() + timeout
    while DFU_ID not in dfu_util(["-l"], quiet=True)[1]:
        if time.time() > end:
            raise SystemExit(f"no DFU device ({DFU_ID}) appeared; see release/FLASHING.md (BOOT + RESET)")
        time.sleep(0.5)
    rc, out = dfu_util(["-a", "0", "-s", "0x08100000", "-D", bank2])
    if rc != 0:
        raise SystemExit(f"flashing bank 2 failed (dfu-util {rc}); the calculator stays in DFU: run calcctl flash "
                         "again or follow release/FLASHING.md")
    rc, out = dfu_util(["-a", "0", "-s", "0x08000000:leave", "-D", bank1])
    # with :leave the board resets at once, so dfu-util often ends with "Error during download get_status":
    # expected (FLASHING.md); the download itself succeeded if it says so
    if rc != 0 and "File downloaded successfully" not in out and "Download done" not in out:
        raise SystemExit(f"flashing bank 1 failed (dfu-util {rc}); the calculator stays in DFU: run calcctl flash "
                         "again or follow release/FLASHING.md")
    print("flashed; waiting for the calculator to start ...")
    try:
        port = find_port(15.0)
        print(f"calculator back on {port}")
    except SystemExit as e:
        print(f"(not seen on USB yet: {e})")
    return 0


def rle_decode(data, npx):
    """Decode the firmware's PackBits-style RLE (core/rle.h) into npx RGB565
    pixel values. Header byte h: h & 0x80 = run of (h & 0x7F) + 1 copies of
    the next little-endian pixel, else h + 1 literal pixels follow."""
    out = []
    i = 0
    while i < len(data):
        h = data[i]
        i += 1
        n = (h & 0x7F) + 1
        if h & 0x80:
            out.extend([data[i] | data[i + 1] << 8] * n)
            i += 2
        else:
            for _ in range(n):
                out.append(data[i] | data[i + 1] << 8)
                i += 2
    if len(out) != npx:
        raise ValueError(f"decoded {len(out)} pixels, expected {npx}")
    return out


def write_png(path, px, w, h):
    """Write RGB565 pixel values as an 8-bit RGB PNG (channels widened by bit
    replication, as in platform/sim/png.c)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            p = px[y * w + x]
            r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
            raw += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def screenshot(link, path, timeout=60.0):
    """Fetch the framebuffer with the console `screen` command and save it.

    Reply: "BEGIN SCREEN w= h= fmt=RGB565LE enc=rle|raw bytes= crc32=", base64
    lines, "END", "OK". The CRC-32 covers the decoded little-endian pixel
    bytes, so a corrupted transfer is detected. The long default timeout
    allows for a raw dump at 115200 baud (150 KB as base64 takes ~18 s).
    """
    ok, lines = command(link, "screen", timeout)
    if not ok:
        raise SystemExit("screen failed: " + " ".join(lines))
    hdr = lines[0].split()
    assert hdr[:2] == ["BEGIN", "SCREEN"], lines[0]
    meta = dict(kv.split("=", 1) for kv in hdr[2:])
    w, h = int(meta["w"]), int(meta["h"])
    body = base64.b64decode("".join(l for l in lines[1:] if l != "END"))
    px = rle_decode(body, w * h) if meta["enc"] == "rle" else list(struct.unpack(f"<{w*h}H", body))
    crc = zlib.crc32(struct.pack(f"<{w*h}H", *px)) & 0xFFFFFFFF
    if crc != int(meta["crc32"], 16):
        raise SystemExit(f"CRC mismatch: {crc:08x} != {meta['crc32']}")
    write_png(path, px, w, h)
    print(f"wrote {path} ({len(body)} bytes compressed)")


def main():
    """Run the command-line commands, then --shot, then interactive mode."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--port", help="serial device (default: the calculator's USB port, found by VID:PID)")
    src.add_argument("--sim", help="path to calcsim")
    ap.add_argument("--sim-arg", action="append", default=[], help="extra calcsim argument (e.g. --no-content)")
    ap.add_argument("--list", action="store_true", help="list the calculators' USB serial ports and exit")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--shot", help="write a PNG of the screen after the commands")
    ap.add_argument("-i", "--interactive", action="store_true")
    ap.add_argument("--upload-module", action="append", default=[], metavar="FILE.MOD",
                    help="install a content module over the console (before the commands)")
    ap.add_argument("--force", action="store_true", help="--upload-module: skip the firmware ABI check")
    ap.add_argument("--window", type=int, default=0,
                    help="--upload-module: lines in flight (default 8 on the calculator's USB port, else 1)")
    ap.add_argument("--release", default=os.path.join(TOOLS, "..", "release"), help="flash: the release folder")
    ap.add_argument("--core", action="store_true", help="flash: the core-tier rollback images")
    ap.add_argument("commands", nargs="*", help="console commands, or `flash`")
    a = ap.parse_args()
    if a.list:
        for p in find_ports():
            print(p)
        return 0
    usb = False
    if not a.port and not a.sim:
        a.port, usb = find_port(), True
    elif a.port:
        usb = a.port in find_ports()
    link = SerialLink(a.port, a.baud) if a.port else SimLink(a.sim, a.sim_arg)
    if a.port:
        link.write(b"\n")  # flush any partial line on the device
        time.sleep(0.1)
        link.buf = b""
    if a.commands[:1] == ["flash"]:
        if not a.port:
            raise SystemExit("flash needs the calculator on USB (not --sim)")
        return flash(link, a.release, a.core)
    status = 0
    window = a.window or (8 if usb else 1)
    for m in a.upload_module:
        ok, msg = upload_module(link, m, a.force, a.timeout, window)
        print(f"upload {m}: {'OK' if ok else 'FAILED'}: {msg}")
        if not ok:
            status = 1
    for c in a.commands:
        ok, lines = command(link, c, a.timeout)
        for l in lines:
            print(l)
        if not ok:
            status = 1
    if a.shot:
        screenshot(link, a.shot)
    if a.interactive:
        for line in sys.stdin:
            ok, lines = command(link, line.rstrip("\n"), a.timeout)
            print("\n".join(lines + ([] if not ok else ["OK"])))
    if a.sim:
        link.write(b"quit\n")
    return status


if __name__ == "__main__":
    sys.exit(main())
