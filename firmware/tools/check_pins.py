#!/usr/bin/env python3
"""Check the firmware pin table (platform/stm32h743/board.h) against
  1. hardware/pinmap/pinmap.py (ASSIGN, KEY_ROWS, KEY_COLS): same net -> pin, and
  2. ST's open pin data: the AF number used for each alternate-function pin is
     the one ST lists for that signal on that pin (STM32H743ZGTx).
Also checks that core/gen/keymap_gen.h row/column pins match board.h.
Exit status 0 = all good.

Run by `make check-pins`. The point is to catch the classic bring-up bugs
before there is a board: a pin moved in the schematic but not in firmware, or
a wrong AF number (RM0433 GPIO AFRL/AFRH; the per-pin AF table is in the
DS12110 datasheet, which ST's open pin data XML encodes). Expected signal
names come from pinmap.py ASSIGN (net -> (pin, ST signal name, note)); key
rows are plain outputs and key columns EXTI inputs.

Inputs outside the firmware tree: ST's STM32_open_pin_data repository
(MCU pin list and the GPIO IP modes file, which ST shares between the H743
and H747). It is not committed (.gitignore); clone
https://github.com/STMicroelectronics/STM32_open_pin_data into
<repo>/tools/STM32_open_pin_data, or point $STM32_OPEN_PIN_DATA at a clone.
"""
import ast
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.join(HERE, "..")
ROOT = os.path.join(FW, "..")
BOARD_H = os.path.join(FW, "platform/stm32h743/board.h")
KEYMAP_H = os.path.join(FW, "core/gen/keymap_gen.h")
PINMAP_PY = os.path.join(ROOT, "hardware/pinmap/pinmap.py")
PIN_DATA = os.environ.get("STM32_OPEN_PIN_DATA") or os.path.join(ROOT, "tools/STM32_open_pin_data")
GPIO_XML = os.path.join(PIN_DATA, "mcu/IP/GPIO-STM32H747_gpio_v1_0_Modes.xml")
MCU_XML = os.path.join(PIN_DATA, "mcu/STM32H743ZGTx.xml")


def literal(path, name):
    """Value of the literal assigned to `name` in a Python file (parsed, not run)."""
    tree = ast.parse(open(path).read(), path)
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise SystemExit(f"{name} not found in {path}")


def board_pins():
    """{net: ("PB10", af)} from the PIN(net, port, number, af) entries in
    board.h; af is an AF number as text or a mode macro such as PIN_AN."""
    pins = {}
    for m in re.finditer(r"PIN\((\w+),\s*([A-K]),\s*(\d+),\s*(\w+)\)", open(BOARD_H).read()):
        net, port, num, af = m.groups()
        pins[net] = (f"P{port}{int(num)}", af)
    return pins


def st_af_table():
    """{(pin, signal name): AF number} from the GPIO modes XML, where each
    GPIO_Pin lists its PinSignals with a PossibleValue "GPIO_AFn_..."."""
    root = ET.parse(GPIO_XML).getroot()
    table = {}
    for p in root.iter():
        if not p.tag.endswith("GPIO_Pin"):
            continue
        name = re.match(r"P[A-K]\d+", p.get("Name") or "")
        if not name:
            continue
        for s in p:
            if not s.tag.endswith("PinSignal"):
                continue
            for v in s.iter():
                if v.tag.endswith("PossibleValue") and v.text and v.text.startswith("GPIO_AF"):
                    af = int(re.match(r"GPIO_AF(\d+)_", v.text).group(1))
                    table[(name.group(0), s.get("Name"))] = af
    return table


def mcu_pins():
    """Set of GPIO pin names ("PA0", ...) that exist on the LQFP144
    STM32H743ZGTx package."""
    root = ET.parse(MCU_XML).getroot()
    names = set()
    for p in root.iter():
        if p.tag.endswith("Pin"):
            m = re.match(r"P[A-K]\d+", p.get("Name") or "")
            if m:
                names.add(m.group(0))
    return names


def main():
    """Run all checks, print warnings/errors and a summary; 1 on any error."""
    errors = []
    if not (os.path.exists(GPIO_XML) and os.path.exists(MCU_XML)):
        raise SystemExit(f"ST pin data not found in {os.path.normpath(PIN_DATA)}: clone "
                         "https://github.com/STMicroelectronics/STM32_open_pin_data there "
                         "or set STM32_OPEN_PIN_DATA")
    board = board_pins()
    assign = literal(PINMAP_PY, "ASSIGN")
    rows = literal(PINMAP_PY, "KEY_ROWS")
    cols = literal(PINMAP_PY, "KEY_COLS")
    expected = {net: (pin, sig) for net, (pin, sig, _n) in assign.items()}
    for i, p in enumerate(rows):
        expected[f"KEY_ROW{i}"] = (p, "GPIO_Output")
    for i, p in enumerate(cols):
        expected[f"KEY_COL{i}"] = (p, "GPIO_EXTI")

    for net, (pin, sig) in expected.items():
        if net not in board:
            errors.append(f"{net}: in pinmap.py ({pin}) but missing from board.h")
            continue
        bpin, af = board[net]
        if bpin != pin:
            errors.append(f"{net}: board.h has {bpin}, pinmap.py has {pin}")
    for net in board:
        if net not in expected:
            errors.append(f"{net}: in board.h but not in pinmap.py")

    st = st_af_table()
    valid_pins = mcu_pins()
    checked = 0
    for net, (pin, af) in board.items():
        if pin not in valid_pins:
            errors.append(f"{net}: {pin} is not a pin of STM32H743ZGTx")
        sig = expected.get(net, (None, None))[1]
        if af.isdigit():
            want = st.get((pin, sig))
            if want is None:
                errors.append(f"{net}: ST data has no AF for {sig} on {pin}")
            elif want != int(af):
                errors.append(f"{net}: {sig} on {pin} is AF{want} per ST, board.h says AF{af}")
            else:
                checked += 1
        else:
            # no AF number: only valid for plain GPIO, analog, oscillator
            # and wake-up pins
            if sig and not (sig.startswith("GPIO") or sig.startswith("ADC") or sig.startswith("DAC")
                            or sig.startswith("RCC_OSC") or sig.startswith("PWR_WKUP")):
                errors.append(f"{net}: {sig} needs an AF but board.h says {af}")
            if sig and (sig.startswith("ADC") or sig.startswith("DAC")) and af != "PIN_AN":
                errors.append(f"{net}: analog signal {sig} should be PIN_AN")

    # keymap header row/col pins
    km = open(KEYMAP_H).read()
    def arr(name):
        m = re.search(rf"#define {name}\s*\{{([^}}]*)\}}", km)
        return [x.strip().strip("'") for x in m.group(1).split(",")]
    kr = [f"P{p}{n}" for p, n in zip(arr("KEY_ROW_PORTS"), arr("KEY_ROW_PINS"))]
    kc = [f"P{p}{n}" for p, n in zip(arr("KEY_COL_PORTS"), arr("KEY_COL_PINS"))]
    for i, p in enumerate(kr):
        if board.get(f"KEY_ROW{i}", ("",))[0] != p:
            errors.append(f"KEY_ROW{i}: keymap_gen.h {p} != board.h")
    for i, p in enumerate(kc):
        if board.get(f"KEY_COL{i}", ("",))[0] != p:
            errors.append(f"KEY_COL{i}: keymap_gen.h {p} != board.h")

    # EXTI line sharing (informational): EXTI line n is shared by pin n of
    # every port (SYSCFG_EXTICRx selects one port per line, RM0433), so two
    # interrupt inputs with the same pin number cannot both have their own
    # edge interrupt
    exti = {}
    for net, (pin, af) in board.items():
        sig = expected.get(net, (None, ""))[1] or ""
        if "EXTI" in sig:
            exti.setdefault(int(pin[2:]), []).append(f"{net}({pin})")
    warnings = [f"EXTI{line} shared by {', '.join(n)}" for line, n in sorted(exti.items()) if len(n) > 1]

    for w in warnings:
        print("warning:", w)
    for e in errors:
        print("error:", e)
    print(f"check_pins: {len(board)} pins, {checked} AF assignments verified against ST data, "
          f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
