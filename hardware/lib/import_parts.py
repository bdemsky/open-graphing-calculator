#!/usr/bin/env python3
"""Import JLC-specific symbols/footprints/3D models with easyeda2kicad.

EasyEDA symbols mark most pins 'unspecified' or 'input', which makes KiCad's
ERC noisy (and flags battery terminals as undriven inputs). After import,
pins of these parts are normalized to 'passive', except power pins of ICs.

Run as "build.sh import" (not part of a normal build): downloads each LCSC
part in PARTS from EasyEDA into hardware/lib/calc.kicad_sym, calc.pretty/ and
calc.3dshapes/, then post-processes the result:
  - pin electrical types normalized (see above; "OUT" of an IC = power_out),
  - footprints upgraded to the current KiCad format,
  - 3D model paths rewritten to project-relative STEP files,
  - measured corrections: TF-01A signal-pad shift, connector model offsets,
  - unnumbered locating pegs turned into NPTH.
These libraries are committed, so the schematic and board build without
network access. Re-running overwrites any hand edits in them.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EASYEDA = os.path.join(HERE, "../../.venv/bin/easyeda2kicad")
LIB = os.path.join(HERE, "calc")
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

PARTS = {
    "C964881": "BH-AAA-B5AA001 2xAAA holder",
    "C62246": "W9825G6KH-6 SDRAM",
    "C55136": "SY6280AAC load switch",
    "C91145": "TF-01A microSD",
    "C165948": "TYPE-C-31-M-12 USB-C",
    "C262686": "AFC24-S40FIA-00 40p FPC",
    "C318884": "TS-1187A-B-A-B tactile switch",
    "C49851": "INA226AIDGSR power monitor",
    "C20345924": "TLV3012BIDBVR comparator + 1.24 V reference (VBUS detect)",
}
POWER_PINS = {"VDD", "VDDQ", "VSS", "VSSQ", "IN", "GND", "VS+", "V+", "V-", "V+/VCC", "VCC"}  # kept as power_in on ICs
ICS = {"W9825G6KH-6", "SY6280AAC", "INA226AIDGSR"}
# The EasyEDA STEP models of some connectors are not centred on their footprints. Measured by
# registering the models' solder-tail faces against the pads (enclosure validation, 2026-10):
# model offset in the footprint's 3D frame (x right, y up = -pad y), mm. Models only; pads unchanged.
MODEL_OFFSET = {
    "TF-SMD_TF-01A": (0, -4.62, 0),                     # J2 microSD: body/shell tabs on the shell pads
    "USB-C_SMD-TYPE-C-31-M-12_1": (0, -2.41, 0),        # J4 USB-C: mouth flush with the front pads
    "FPC-SMD_P0.50-40P_QCHF-SM-H2.0": (0, 0.53, 0),     # J3 LCD FPC
    "FPC-SMD_6P-P1.00_522710679": (0, -2.35, 0),        # J7 touch FPC (removed with the 2.8" panel)
}


def set_model_offset(text, name):
    """Replace the (offset (xyz ...)) of footprint name's STEP model with MODEL_OFFSET[name]."""
    x, y, z = MODEL_OFFSET[name]
    return re.sub(r'(\(model "[^"]*/' + re.escape(name) + r'\.step"\s*\(offset\s*\(xyz )[^)]*\)',
                  lambda m: m.group(1) + f"{x:g} {y:g} {z:g})", text)


def normalize(path):
    """Rewrite every pin's electrical type in the symbol library at path.

    Each top-level symbol block (two-space indented, as easyeda2kicad writes
    it) is processed separately so the IC check uses the symbol's name.
    """
    text = open(path, encoding="utf-8").read()
    out = []
    for m in re.finditer(r'(\n  \(symbol "([^"]+)".*?\n  \))', text, re.S):
        block, name = m.group(1), m.group(2)

        def fix(pm):
            etype, rest = pm.group(1), pm.group(2)
            pin_name = re.search(r'\(name "([^"]*)"', rest).group(1)
            if name in ICS and pin_name in POWER_PINS:
                new = "power_in"
            elif name in ICS and pin_name == "OUT":
                new = "power_out"
            else:
                new = "passive"
            return f"(pin {new} {rest}"

        block = re.sub(r"\(pin (\w+) (.*?\(number \"[^\"]*\")", lambda pm: fix(pm), block, flags=re.S)
        out.append((m.group(1), block))
    for old, new in out:
        text = text.replace(old, new)
    open(path, "w", encoding="utf-8").write(text)


def main():
    """Import every part in PARTS and post-process the library (see module doc)."""
    for lcsc in PARTS:
        r = subprocess.run([EASYEDA, "--full", f"--lcsc_id={lcsc}", "--output", LIB, "--overwrite"],
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stdout, r.stderr)
            sys.exit(f"import of {lcsc} failed")
    normalize(LIB + ".kicad_sym")
    # easyeda2kicad writes legacy "(module ...)" footprints; resave in the current format.
    subprocess.run([KICAD_CLI, "fp", "upgrade", "--force", LIB + ".pretty"], check=True,
                   capture_output=True)
    # Point 3D models at the STEP files relative to the project (portable, and STEP
    # keeps exact geometry for the enclosure design).
    for f in os.listdir(LIB + ".pretty"):
        path = os.path.join(LIB + ".pretty", f)
        text = open(path, encoding="utf-8").read()
        text = re.sub(r'\(model "[^"]*/calc\.3dshapes/([^"/]+)\.wrl"',
                      r'(model "${KIPRJMOD}/../lib/calc.3dshapes/\1.step"', text)
        # TF-01A: the HRO drawing puts the signal row 0.10 mm further toward -x relative to
        # the shell tabs than the EasyEDA footprint does (CD is 1.10 from the tab, not 1.20).
        # Signal row = pads 1-9 (DAT2..DAT1, CD); 10-13 are the shell tabs and stay put.
        if f.startswith("TF-SMD_TF-01A"):
            def shift(m):
                return f'(pad "{m.group(1)}" smd{m.group(2)}(at {float(m.group(3)) - 0.1:.4g} '
            text = re.sub(r'\(pad "([1-9])" smd(.*?)\(at (-?[\d.]+) ', shift, text, flags=re.S)
        name = f[:-len(".kicad_mod")]
        if name in MODEL_OFFSET:
            text = set_model_offset(text, name)
        # Unnumbered locating pegs come in as plated holes with no copper ring: make them NPTH.
        text = re.sub(r'\(pad "" thru_hole (circle|oval)', r'(pad "" np_thru_hole \1', text)
        open(path, "w", encoding="utf-8").write(text)
    print(f"imported {len(PARTS)} parts into {os.path.relpath(LIB)}.*")


if __name__ == "__main__":
    main()
