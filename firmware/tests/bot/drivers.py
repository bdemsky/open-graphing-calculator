"""Drivers for the Sparkle Town playtest bot: the simulator console and the Mac page.

Both drivers speak the same small interface, so every player strategy and every invariant runs on
both platforms:

    d.boot(seed)                 a fresh calculator with Sparkle Town open on the title screen
    d.con(line) -> [lines]       one console line (the module's `mq ...` hooks, `state`, `text`, ...)
    d.state() -> {key: value}    every state key
    d.texts() -> [Run]           the text on screen, with boxes (logical 320x240 coordinates)
    d.hits() -> Hits             every tap target: buttons, game widgets, the right option
    d.tap(x, y) / d.drag(...) / d.key(NAME) / d.wheel(x, y, notches) / d.pinch(cx, cy, d1, d2)
    d.step(n)                    n module frames of 40 ms (simulated time)
    d.pixels() -> bytes          the 320x240 RGB565 framebuffer (for feedback / parity checks)
    d.shot(path)                 a screenshot PNG
    d.power_cycle() / d.reload() the progress must survive these

Every input is also appended to d.script (the repro script: replayable with `bot.py replay`).
"""
import atexit
import base64
import json
import os
import re
import select
import subprocess
import time
import zlib

FW = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SIM = os.path.join(FW, "build", "sim", "calcsim")


class Crash(Exception):
    """The program under test died, hung or threw."""


class Run:
    __slots__ = ("x", "y", "w", "h", "s", "squeeze")

    def __init__(self, x, y, w, h, s, squeeze=1.0):
        self.x, self.y, self.w, self.h, self.s, self.squeeze = x, y, w, h, s, squeeze

    def __repr__(self):
        return f"{self.s!r}@{self.x},{self.y} {self.w}x{self.h}"


class Target:
    __slots__ = ("kind", "id", "arg", "x", "y", "w", "h", "value", "label", "src")

    def __init__(self, src, kind, id_, arg, x, y, w, h, value=0, label=""):
        self.src, self.kind, self.id, self.arg = src, kind, id_, arg
        self.x, self.y, self.w, self.h, self.value, self.label = x, y, w, h, value, label

    @property
    def center(self):
        return (max(0, min(319, self.x + self.w // 2)), max(0, min(239, self.y + self.h // 2)))

    def key(self):
        return (self.src, self.kind, self.id, self.arg)

    def __repr__(self):
        return f"{self.src}:{self.kind}#{self.id}/{self.arg}@{self.x},{self.y} {self.w}x{self.h}" + (
            f" '{self.label}'" if self.label else "")


class Hits:
    def __init__(self):
        self.targets = []
        self.game = None  # dict(sub, nopt, right, gone, opts)

    def __iter__(self):
        return iter(self.targets)

    def __len__(self):
        return len(self.targets)


_BTN = re.compile(r"^btn (\d+) id (\d+) arg (\d+) at (-?\d+),(-?\d+) (\d+)x(\d+)$")
_WG = re.compile(r"^(\d+) (\w+) id (\d+) arg (-?\d+) at (-?\d+),(-?\d+) (\d+)x(\d+) \(hit (\d+)x(\d+)\) value (-?\d+) ?(.*?)( \[focus\])?$")
_GAME = re.compile(r"^game sub (\d+) nopt (\d+) right (-?\d+) gone (\d+) opts (-?\d+) (-?\d+) (-?\d+) (-?\d+)(.*)$")


def parse_hits(lines):
    h = Hits()
    for l in lines:
        m = _BTN.match(l)
        if m:
            i, id_, arg, x, y, w, hh = map(int, m.groups())
            h.targets.append(Target("btn", "button", id_, arg, x, y, w, hh))
            continue
        m = _GAME.match(l)
        if m:
            g = list(map(int, m.groups()[:8]))
            h.game = dict(sub=g[0], nopt=g[1], right=g[2], gone=g[3], opts=g[4:4 + g[1]], opt4=g[4:8])
            rest = m.group(9).split()
            for i in range(0, len(rest) - 1, 2):
                h.game[rest[i]] = int(rest[i + 1])
            continue
        m = _WG.match(l)
        if m:
            i, kind, id_, arg, x, y, w, hh, hw, hh2, val, label, _ = m.groups()
            h.targets.append(Target("wg", kind, int(id_), int(arg), int(x), int(y), int(w), int(hh), int(val), label.strip()))
    return h


_RUN = re.compile(r"^\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+) (.*)$")


def parse_runs(lines):
    out = []
    for l in lines:
        m = _RUN.match(l)
        if m:
            y, x, w, h, s = m.groups()
            out.append(Run(int(x), int(y), int(w), int(h), s))
    return out


def decode_screen(lines):
    """`screen` output -> 320*240*2 bytes of RGB565 LE."""
    b64 = []
    on = False
    for l in lines:
        if l.startswith("BEGIN SCREEN"):
            on = True
            continue
        if l == "END":
            break
        if on:
            b64.append(l.strip())
    raw = base64.b64decode("".join(b64))
    out = bytearray()
    i = 0
    while i < len(raw):
        h = raw[i]
        i += 1
        if h & 0x80:
            out += raw[i:i + 2] * ((h & 0x7F) + 1)
            i += 2
        else:
            n = (h + 1) * 2
            out += raw[i:i + n]
            i += n
    return bytes(out)


class Driver:
    platform = "?"
    scale = 1

    def __init__(self):
        self.script = []  # the inputs since boot: the repro
        self.seed = 0

    # inputs: recorded
    def rec(self, line):
        self.script.append(line)

    def tap(self, x, y):
        self.rec(f"tap {x} {y}")
        self._tap(x, y)

    def down(self, x, y, how="mouse"):
        """the first half of a press: the finger lands (the bot looks in between; up() lifts it)"""
        self.rec(f"down {x} {y} {how}")
        self._down(x, y, how)

    def up(self, x, y, how="mouse"):
        self.rec(f"up {x} {y} {how}")
        self._up(x, y, how)

    def drag(self, x1, y1, x2, y2, steps=6):
        self.rec(f"drag {x1} {y1} {x2} {y2} {steps}")
        self._drag(x1, y1, x2, y2, steps)

    def key(self, name):
        self.rec(f"key {name}")
        self._key(name)

    def wheel(self, x, y, notches):
        self.rec(f"wheel {x} {y} {notches}")
        self._wheel(x, y, notches)

    def pinch(self, cx, cy, d1, d2):
        self.rec(f"pinch {cx} {cy} {d1} {d2}")
        self._pinch(cx, cy, d1, d2)

    def step(self, n=1):
        self.rec(f"step {n}")
        self.con(f"mq step {n}")

    def setup(self, line):
        """a console line that is part of the repro (mq seed/new/level/...)"""
        self.rec(f"con {line}")
        return self.con(line)

    def replay(self, line):
        op, _, rest = line.partition(" ")
        a = rest.split()
        if op == "tap":
            self._tap(int(a[0]), int(a[1]))
        elif op == "down":
            self._down(int(a[0]), int(a[1]), a[2] if len(a) > 2 else "mouse")
        elif op == "up":
            self._up(int(a[0]), int(a[1]), a[2] if len(a) > 2 else "mouse")
        elif op == "gap":
            self.gap(int(a[0]))
            return self.script.append(line)
        elif op == "drag":
            self._drag(*map(int, a))
        elif op == "key":
            self._key(a[0])
        elif op == "wheel":
            self._wheel(*map(int, a))
        elif op == "pinch":
            self._pinch(*map(int, a))
        elif op == "step":
            self.con(f"mq step {a[0]}")
        elif op == "obs":
            self.state(), self.texts(), self.hits()
            return self.script.append(line)
        elif op == "hold":
            self.hold(*map(int, a))
        elif op == "con":
            self.con(rest)
        elif op == "power_cycle":
            self.power_cycle()
        elif op == "reload":
            self.reload()
        self.script.append(line)

    def state(self):
        st = {}
        for l in self.con("state"):
            k, eq, v = l.partition("=")
            if eq:
                st[k] = v
        return st

    def hits(self):
        return parse_hits(self.con("mq hits"))


class SimDriver(Driver):
    """The simulator (build/sim/calcsim) over its stdin/stdout console. ASan/UBSan reports and exits
    are crashes; a command that does not answer within `timeout` seconds is a hang."""
    platform = "sim"

    def __init__(self, shots, timeout=20.0, sim=SIM, flash=None):
        super().__init__()
        self.shots, self.timeout, self.simbin = shots, timeout, sim
        os.makedirs(shots, exist_ok=True)
        self.flash = flash or os.path.join(shots, f"flash_{os.getpid()}_{id(self)}.bin")
        self.p = None
        self.stderr_path = os.path.join(shots, f"stderr_{os.getpid()}_{id(self)}.txt")

    def _start(self, fresh):
        if fresh and os.path.exists(self.flash):
            os.remove(self.flash)
        self.errf = open(self.stderr_path, "w")
        self.p = subprocess.Popen([self.simbin, "--shots", self.shots, "--flash", self.flash], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.errf, bufsize=0)
        self.buf = b""
        self._readline()  # the banner

    def _readline(self):
        t0 = time.time()
        while b"\n" not in self.buf:
            r, _, _ = select.select([self.p.stdout], [], [], max(0.0, self.timeout - (time.time() - t0)))
            if not r:
                raise Crash(f"hang: no answer within {self.timeout:.0f} s")
            chunk = os.read(self.p.stdout.fileno(), 65536)
            if not chunk:
                raise Crash("the simulator exited: " + self._stderr_tail())
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line.decode("utf-8", "replace")

    def _stderr_tail(self):
        try:
            self.errf.flush()
            s = open(self.stderr_path, errors="replace").read()
            m = re.search(r"(ERROR: AddressSanitizer.*|runtime error:.*)", s)
            return (m.group(1) if m else s[-400:]).strip()[:600]
        except OSError:
            return "?"

    def con(self, line):
        if self.p is None or self.p.poll() is not None:
            raise Crash("the simulator is not running: " + self._stderr_tail())
        try:
            self.p.stdin.write((line + "\n").encode())
            self.p.stdin.flush()
        except BrokenPipeError:
            raise Crash("the simulator exited: " + self._stderr_tail())
        out = []
        while True:
            l = self._readline()
            if l == "OK":
                return out
            if l.startswith("ERR"):
                out.append(l)
                return out
            if l.startswith("# ") and ("fault" in l.lower() or "panic" in l.lower()):
                raise Crash(l)
            out.append(l)

    def boot(self, seed, fresh=True):
        self.close()
        self.seed = seed
        self.script = [f"boot {seed}"]
        self._start(fresh)
        if fresh:
            for c in ["fs insert 64", "fs format", "module library extra", "module install /CALC/MODULES/MATHQ010.MOD"]:
                self.con(c)
        self.con("key CLEAR")
        self.con("app mathquest")
        self.con(f"mq seed {seed}")

    def close(self):
        if self.p and self.p.poll() is None:
            try:
                self.p.stdin.close()
                self.p.wait(timeout=5)
            except Exception:
                self.p.kill()
        self.p = None

    def power_cycle(self):
        """`reboot`: the app restarts from the saved session, as after a reset."""
        self.rec("power_cycle")
        self.con("reboot")
        self.con("app mathquest")

    def reload(self):
        """the whole process again over the same flash image (a cold start)"""
        self.rec("reload")
        self.con("session save")
        self.close()
        self._start(False)
        self.con("app mathquest")

    def texts(self):
        return [r for r in parse_runs(self.con("text wh"))]

    def _tap(self, x, y):
        self.con(f"touch tap {x} {y}")

    def _down(self, x, y, how="mouse"):
        self.con(f"touch {x} {y} down")

    def _up(self, x, y, how="mouse"):
        self.con(f"touch {x} {y} up")

    def gap(self, frames):
        """the game's time while a finger is down (frames of 40 ms)"""
        self.con(f"mq step {frames}")

    def _drag(self, x1, y1, x2, y2, steps=6):
        self.con(f"touch drag {x1} {y1} {x2} {y2} {steps}")

    def _key(self, name):
        self.con(f"key {name}")

    def _wheel(self, x, y, n):
        self.con(f"mq scroll {n} {x} {y}")

    def _pinch(self, cx, cy, d1, d2):
        self.con(f"touch pinch {cx} {cy} {d1} {d2}")

    def hold(self, x, y, ms):
        self.con(f"touch {x} {y} down")
        self.con(f"wait {ms}")
        self.con(f"touch {x} {y} up")

    def crc(self):
        out = self.con("fbcrc")
        return out[-1] if out else ""

    def pixels(self):
        return decode_screen(self.con("screen"))

    def shot(self, path):
        self.con(f"screenshot {os.path.abspath(path)}")
        return path


class MacDriver(Driver):
    """The Mac page (web/dist/sparkle.html, or a private home page) in headless Chrome, through
    mac_bridge.js (CDP): real pointer, wheel, pinch (ctrl+wheel) and key events at the page's
    devicePixelRatio, the renderer's texts with their boxes, the canvas against the 1x framebuffer."""
    platform = "mac"

    def __init__(self, shots, page=None, w=1470, h=956, dpr=2, timeout=60.0):
        super().__init__()
        self.shots, self.timeout = shots, timeout
        os.makedirs(shots, exist_ok=True)
        self.page = page or os.path.join(FW, "web", "dist", "sparkle.html")
        self.p = subprocess.Popen(["node", os.path.join(os.path.dirname(__file__), "mac_bridge.js"), "--page", self.page,
                                   "--w", str(w), "--h", str(h), "--dpr", str(dpr)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.buf = b""
        self.scale = dpr
        atexit.register(self.close)  # (never a browser left behind by a bot that stopped early)
        r = self._rpc("hello")
        self.scale = r.get("S", dpr)

    def _rpc(self, op, **kw):
        if self.p.poll() is not None:
            raise Crash("the Mac bridge exited: " + self.p.stderr.read().decode(errors="replace")[-600:])
        self.p.stdin.write((json.dumps(dict(op=op, **kw)) + "\n").encode())
        self.p.stdin.flush()
        t0 = time.time()
        while b"\n" not in self.buf:
            r, _, _ = select.select([self.p.stdout], [], [], max(0.0, self.timeout - (time.time() - t0)))
            if not r:
                raise Crash(f"hang: the page did not answer {op} within {self.timeout:.0f} s")
            chunk = os.read(self.p.stdout.fileno(), 1 << 20)
            if not chunk:
                raise Crash("the Mac bridge exited: " + self.p.stderr.read().decode(errors="replace")[-600:])
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        res = json.loads(line)
        if res.get("errors"):
            raise Crash("page exception: " + " | ".join(res["errors"])[:600])
        if "fail" in res:
            raise Crash("bridge: " + res["fail"])
        return res

    def con(self, line):
        out = self._rpc("con", line=line)["out"]
        lines = [l for l in out.split("\n") if l]
        if lines and lines[-1] == "OK":
            lines.pop()
        return lines

    def boot(self, seed, fresh=True):
        self.seed = seed
        self.script = [f"boot {seed}"]
        self._rpc("boot", fresh=fresh)
        self.con(f"mq seed {seed}")

    def close(self):
        """the bridge quits and its browser goes with it (tools/headless_chrome.js); a SIGTERM lets it clean up, a
        kill only when it will not (a killed bridge would leave its browser behind)"""
        if not self.p or self.p.poll() is not None:
            return
        try:
            self._rpc("quit")
        except Exception:
            pass
        for sig in (None, "term", "kill"):
            try:
                if sig == "term":
                    self.p.terminate()
                elif sig == "kill":
                    self.p.kill()
                self.p.wait(timeout=10)
                return
            except Exception:
                continue

    def power_cycle(self):
        self.rec("power_cycle")
        self.con("reboot")
        self.con("app mathquest")

    def reload(self):
        self.rec("reload")
        self._rpc("reload")

    def texts(self):
        return [Run(t["x"], t["y"], t["w"], t["h"], t["s"], t.get("q", 1.0)) for t in self._rpc("texts")["texts"]]

    def _tap(self, x, y):
        self._rpc("click", x=x, y=y)

    def _down(self, x, y, how="mouse"):
        self._rpc("down", x=x, y=y, how=how)

    def _up(self, x, y, how="mouse"):
        self._rpc("up", x=x, y=y, how=how)

    def gap(self, frames):
        """the page runs in real time: a finger down for `frames` x 40 ms"""
        time.sleep(frames * 0.04)

    def _drag(self, x1, y1, x2, y2, steps=6):
        self._rpc("drag", x1=x1, y1=y1, x2=x2, y2=y2, steps=steps)

    def _key(self, name):
        self._rpc("key", name=name)

    def _wheel(self, x, y, n):
        self._rpc("wheel", x=x, y=y, n=n)

    def _pinch(self, cx, cy, d1, d2):
        self._rpc("pinch", x=cx, y=cy, scale=d2 / max(1, d1))

    def hold(self, x, y, ms):
        self._rpc("hold", x=x, y=y, ms=ms)

    def thumb(self):
        """the Mac canvas's play area as 80x60 luminance values (what she sees, not the 1x copy)"""
        return self._rpc("thumb")["lum"]

    def crc(self):
        out = self.con("fbcrc")
        return out[-1] if out else ""

    def pixels(self):
        return base64.b64decode(self._rpc("fb")["fb"])

    def parity(self):
        """blocks the 1x framebuffer draws that the Mac canvas does not (and the reverse)"""
        return self._rpc("parity")

    def shot(self, path):
        data = self._rpc("shot")["png"]
        open(path, "wb").write(base64.b64decode(data))
        return path


def crc_of(px):
    return zlib.crc32(px)
