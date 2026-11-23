#!/usr/bin/env python3
"""Non-interactive bring-up helper for the calculator board (see hardware/BRINGUP.md).

Wraps OpenOCD (SWD through a CMSIS-DAP probe, e.g. a Pico 2 running debugprobe)
and the UART console (firmware/tools/calcctl.py). Every step has a timeout and
never waits for a keyboard; everything printed is also appended to a log file
(default firmware/build/bringup-logs/bringup-YYYYmmdd.log).

  bringup.py preflight              tools, release files, USB devices (no board needed)
  bringup.py ports                  list /dev/cu.usbmodem* and CMSIS-DAP / DFU USB devices
  bringup.py probe                  is a CMSIS-DAP probe answering? (no board needed)
  bringup.py id                     SWD connect: DBGMCU IDCODE (silicon rev), boot address, option bytes
  bringup.py flash FILE.elf         program + verify + reset (OpenOCD), e.g. firmware/release/bringup.elf
  bringup.py reset                  reset the MCU over SWD (SYSRESETREQ)
  bringup.py log SECONDS [--reset]  print console output for SECONDS (optionally reset first: boot log)
  bringup.py cmd 'CMD' ['CMD'...]   run console commands, wait for each OK/ERR (calcctl.py)
  bringup.py keys SECONDS           collect '# key NAME down' lines (bring-up image), report missing keys

Options: --port /dev/cu.usbmodemXXXX (default: $CALC_PORT, or the only usbmodem port),
--timeout S for console commands (default 15), --speed KHZ to lower the SWD clock,
--log FILE. Exit status 0 = pass, 1 = fail/ERR, 2 = usage or missing tool.
Only the Python standard library is used.
"""
import argparse
import datetime
import glob
import hashlib
import importlib.util
import os
import re
import select
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FW = os.path.join(REPO, "firmware")
OPENOCD = os.environ.get("OPENOCD", "/opt/local/bin/openocd")
CFG = os.path.join(FW, "openocd", "calc-cmsisdap.cfg")
ARM_GCC_GLOB = "/Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc"
REV = {0x1003: "Y (SDCLK 90 MHz)", 0x2003: "V (SDCLK 100 MHz)"}

LOG = None


def out(msg=""):
    print(msg, flush=True)
    if LOG:
        with open(LOG, "a") as f:
            f.write(msg + "\n")


def load_calcctl():
    spec = importlib.util.spec_from_file_location("calcctl", os.path.join(FW, "tools", "calcctl.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def find_port(arg):
    if arg:
        return arg
    if os.environ.get("CALC_PORT"):
        return os.environ["CALC_PORT"]
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) == 1:
        return ports[0]
    out(f"need --port: found {ports or 'no /dev/cu.usbmodem* ports'}")
    sys.exit(2)


def openocd(cmds, timeout=60, speed=None):
    """Run OpenOCD with the board config and the given -c string; return (rc, text)."""
    if not os.path.exists(OPENOCD):
        out(f"FAIL: OpenOCD not found at {OPENOCD} (set $OPENOCD)")
        sys.exit(2)
    argv = [OPENOCD, "-f", CFG]
    if speed:
        argv += ["-c", f"adapter speed {speed}"]
    # One -c per command: within a single "a; b; c" string OpenOCD prints only the
    # last command's result, so the mdw values would be lost.
    for c in cmds.split(";"):
        if c.strip():
            argv += ["-c", c.strip()]
    out("$ " + " ".join(argv))
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, cwd=FW)
        text = p.stdout.decode("utf-8", "replace")
        rc = p.returncode
    except subprocess.TimeoutExpired as e:
        text = (e.stdout or b"").decode("utf-8", "replace") + f"\n(timed out after {timeout} s)"
        rc = 124
    for line in text.splitlines():
        out("  " + line)
    return rc, text


def words(text, addr):
    """Values printed by OpenOCD 'mdw ADDR' (line '0xADDR: value ...')."""
    m = re.search(r"0x%08x:\s+([0-9a-fA-F]+)" % addr, text)
    return int(m.group(1), 16) if m else None


def usb_devices():
    try:
        t = subprocess.run(["ioreg", "-p", "IOUSB", "-w0", "-l"], stdout=subprocess.PIPE, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    names = re.findall(rb'"USB Product Name" = "([^"]*)"', t)
    return [n.decode("utf-8", "replace") for n in names]


# ------------------------------------------------------------------ commands
def c_ports(a):
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    out("serial ports: " + (", ".join(ports) if ports else "none"))
    devs = usb_devices()
    interesting = [d for d in devs if re.search(r"CMSIS|Debugprobe|Picoprobe|DFU|STM32|RP2350|RP2", d, re.I)]
    out("USB devices of interest: " + (", ".join(interesting) if interesting else "none"))
    return 0 if ports or interesting else 1


def c_preflight(a):
    ok = True
    out(f"python {sys.version.split()[0]} (need >= 3.8): " + ("ok" if sys.version_info >= (3, 8) else "FAIL"))
    ok &= sys.version_info >= (3, 8)
    for name, path in (("openocd", OPENOCD),):
        if os.path.exists(path):
            v = subprocess.run([path, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=10).stdout.decode().splitlines()[0]
            out(f"{name}: {path}: {v}")
        else:
            out(f"{name}: MISSING ({path}); install with: sudo port install openocd")
            ok = False
    gcc = sorted(glob.glob(ARM_GCC_GLOB)) or ([shutil.which("arm-none-eabi-gcc")] if shutil.which("arm-none-eabi-gcc") else [])
    out("arm-none-eabi-gcc: " + (gcc[-1] if gcc else "MISSING (Arm GNU Toolchain)"))
    ok &= bool(gcc)
    for tool, why in (("make", "build"), ("dfu-util", "optional: USB DFU fallback"),
                      ("stm32flash", "optional: UART bootloader fallback")):
        out(f"{tool}: {shutil.which(tool) or 'not found'} ({why})")
    ok &= bool(shutil.which("make"))
    rel = os.path.join(FW, "release")
    for f in ("bringup.elf", "calc.elf", "SHA256SUMS", "MANIFEST.txt"):
        out(f"release/{f}: " + ("present" if os.path.exists(os.path.join(rel, f)) else "MISSING"))
    for f in ("calc_qspi.bin", "calc_core.elf", "MODULES/INDEX.TXT"):
        out(f"release/{f}: " + ("present" if os.path.exists(os.path.join(rel, f)) else
                                "missing (run 'make release' in firmware/)"))
    sums = os.path.join(rel, "SHA256SUMS")
    if os.path.exists(sums):
        bad = 0
        for line in open(sums):
            h, _, name = line.strip().partition("  ")
            p = os.path.join(rel, name)
            if not os.path.exists(p) or hashlib.sha256(open(p, "rb").read()).hexdigest() != h:
                bad += 1
                out(f"  checksum mismatch or missing: {name}")
        out("SHA256SUMS: " + ("ok" if not bad else f"{bad} problem(s)"))
        ok &= not bad
    man = os.path.join(rel, "MANIFEST.txt")
    if os.path.exists(man):
        out("MANIFEST: " + open(man).readline().strip())
        src = re.search(r"source revision: (\w+)", open(man).read())
        if src:
            try:
                n = subprocess.run(["git", "-C", REPO, "rev-list", "--count", f"{src.group(1)}..HEAD"],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10).stdout.decode().strip()
                out(f"release built from {src.group(1)}, {n or '?'} commit(s) behind HEAD"
                    + (" -> rebuild with 'make release'" if n and n != "0" else ""))
            except (OSError, subprocess.TimeoutExpired):
                pass
    c_ports(a)
    out("PREFLIGHT " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def c_probe(a):
    # 'init' fails without a target, but the CMSIS-DAP lines print first.
    rc, t = openocd("init; shutdown", timeout=20, speed=a.speed)
    probe = re.search(r"CMSIS-DAP: (FW Version|Interface (Initialised|ready))", t)
    target = re.search(r"stm32h7x\.cpu0.*(Cortex-M7|processor detected)|Examination succeed", t)
    out("probe: " + ("CMSIS-DAP found" if probe else "NOT found (is debugprobe flashed? USB cable has data lines?)"))
    out("target: " + ("STM32H7 answered" if target else "no target (normal without a powered board)"))
    return 0 if probe else 1


def c_id(a):
    rc, t = openocd("init; mdw 0x5C001000; mdw 0x52002040; mdw 0x5200201C; shutdown", timeout=30, speed=a.speed)
    idc = words(t, 0x5C001000)
    if idc is None:
        out("FAIL: no IDCODE read. Board powered? GND, SWCLK, SWDIO wired? Try --speed 1000, "
            "or hold RESET, start the command, release RESET after 'Listening on port'.")
        return 1
    dev, rev = idc & 0xFFF, idc >> 16
    out(f"DBGMCU_IDCODE = 0x{idc:08x}: DEV_ID 0x{dev:03x} ({'STM32H74x/75x' if dev == 0x450 else 'UNEXPECTED'}), "
        f"REV_ID 0x{rev:04x} = rev {REV.get(rev, 'unknown')}")
    boot = words(t, 0x52002040)
    if boot is not None:
        out(f"FLASH_BOOT_CUR = 0x{boot:08x} ({'ok' if boot == 0x1ff00800 else 'expected 0x1ff00800'})")
    opt = words(t, 0x5200201C)
    if opt is not None:
        out(f"FLASH_OPTSR_CUR = 0x{opt:08x}: RDP 0x{(opt >> 8) & 0xFF:02x} "
            f"({'level 0' if ((opt >> 8) & 0xFF) == 0xAA else 'NOT level 0'}), "
            f"FZ_IWDG_STOP={(opt >> 17) & 1} FZ_IWDG_SDBY={(opt >> 18) & 1} (1 = new chip, 0 = frozen)")
    return 0 if dev == 0x450 else 1


def c_flash(a):
    f = os.path.abspath(a.file)
    if not os.path.exists(f):
        out(f"no such file: {f}")
        return 2
    rc, t = openocd("program {%s} verify reset exit" % f, timeout=240, speed=a.speed)
    good = "Verified OK" in t or "** Verified OK **" in t
    out("FLASH " + ("PASS (verified)" if good and rc == 0 else "FAIL"))
    return 0 if good and rc == 0 else 1


def c_reset(a):
    rc, t = openocd("init; reset run; shutdown", timeout=30, speed=a.speed)
    return 0 if rc == 0 else 1


def read_lines(port, seconds, on_line, reset=False, speed=None):
    cc = load_calcctl()
    link = cc.SerialLink(port, 115200)
    proc = None
    if reset:
        proc = subprocess.Popen([OPENOCD, "-f", CFG, "-c", "init; reset run; shutdown"], cwd=FW,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end = time.time() + seconds
    while time.time() < end:
        try:
            line = link.readline(max(0.05, min(1.0, end - time.time())))
        except TimeoutError:
            continue
        if on_line(line):
            break
    if proc:
        try:
            if proc.wait(timeout=20) != 0:
                out("(the OpenOCD reset failed: is the probe connected?)")
        except subprocess.TimeoutExpired:
            proc.kill()
            out("(the OpenOCD reset timed out)")


def c_log(a):
    port = find_port(a.port)
    out(f"listening on {port} for {a.seconds} s" + (" after a reset" if a.reset else ""))
    n = [0]

    def show(line):
        n[0] += 1
        out(line)
        return False

    read_lines(port, a.seconds, show, reset=a.reset)
    out(f"({n[0]} line(s))")
    return 0 if n[0] else 1


def c_cmd(a):
    port = find_port(a.port)
    cc = load_calcctl()
    link = cc.SerialLink(port, 115200)
    bad = 0
    for c in a.commands:
        out(f"> {c}")
        try:
            ok, lines = cc.command(link, c, timeout=a.timeout, quiet=True)
        except TimeoutError:
            out(f"TIMEOUT after {a.timeout} s (no OK/ERR)")
            bad += 1
            continue
        for l in lines:
            out(l)
        out("OK" if ok else "(ERR)")
        bad += not ok
    return 1 if bad else 0


def key_names():
    src = open(os.path.join(FW, "core", "gen", "keymap_gen.c")).read()
    body = re.search(r"key_names\[KEY_COUNT\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    return [n for n in re.findall(r'"([^"]+)"', body) if n != "NONE"]


def c_keys(a):
    port = find_port(a.port)
    want = key_names()
    seen = set()
    out(f"press every key once ({len(want)} incl. ON) within {a.seconds} s; listening on {port}")

    def on(line):
        m = re.match(r"# key (\S+) down", line)
        if m:
            k = m.group(1)
            if k not in seen:
                seen.add(k)
                out(f"  {k:8s} ({len(seen)}/{len(want)})")
        elif line.startswith("# "):
            out(line)
        return seen >= set(want)

    read_lines(port, a.seconds, on)
    missing = [k for k in want if k not in seen]
    extra = sorted(seen - set(want))
    out("missing: " + (" ".join(missing) if missing else "none"))
    if extra:
        out("unexpected names: " + " ".join(extra))
    out("KEYS " + ("PASS" if not missing else "FAIL"))
    return 0 if not missing else 1


def main():
    global LOG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--speed", type=int, help="SWD clock in kHz (config default 4000)")
    ap.add_argument("--log")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("preflight")
    sub.add_parser("ports")
    sub.add_parser("probe")
    sub.add_parser("id")
    p = sub.add_parser("flash")
    p.add_argument("file")
    sub.add_parser("reset")
    p = sub.add_parser("log")
    p.add_argument("seconds", type=float)
    p.add_argument("--reset", action="store_true")
    p = sub.add_parser("cmd")
    p.add_argument("commands", nargs="+")
    p = sub.add_parser("keys")
    p.add_argument("seconds", type=float)
    a = ap.parse_args()
    if a.log:
        LOG = a.log
    else:
        d = os.path.join(FW, "build", "bringup-logs")
        os.makedirs(d, exist_ok=True)
        LOG = os.path.join(d, "bringup-" + datetime.date.today().strftime("%Y%m%d") + ".log")
    with open(LOG, "a") as f:
        f.write(f"\n=== {datetime.datetime.now().isoformat(timespec='seconds')} {' '.join(sys.argv[1:])}\n")
    fn = {"preflight": c_preflight, "ports": c_ports, "probe": c_probe, "id": c_id, "flash": c_flash,
          "reset": c_reset, "log": c_log, "cmd": c_cmd, "keys": c_keys}[a.cmd]
    sys.exit(fn(a))


if __name__ == "__main__":
    main()
