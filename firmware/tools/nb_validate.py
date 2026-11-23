#!/usr/bin/env python3
"""Check the simulator's notebooks with Jupyter's own tools (`make nb-validate`).

The simulator makes notebooks with every kind of cell and output (math, Python with
printed text, a value, LaTeX, an error and a plot, text, graph), imports the test
notebooks of tests/notebooks/, and the computer model copies every file of the CALC
drive's NOTEBOOKS folder to --out (`usb get`). Each file, and each example notebook of
firmware/notebooks/ (built into the Python module), is then read and validated
with the `nbformat` package (nbformat 4.5 schema, cell ids), and one is converted
with `jupyter nbconvert --to markdown` (no browser, no kernel: outputs as stored).

The packages live in a virtualenv under the build directory (--venv; gitignored),
made on first use with pip. Without network or pip the check is skipped (exit 0)
with a note, so `make test` still passes on a machine without them.
"""
import argparse
import os
import subprocess
import sys

SCRIPT = r'''nb store wipe
nb new "Every Kind"
nb set 1 "integrate(x^2,x)"
nb add python "print('printed')\nfrom calc import x\n(x+1)**2"
nb add python "import calc\ncalc.plot('x^3-x', -2, 2)\n'plotted'"
nb add python "1/0"
nb add text "# Heading\nSome **bold** text, $x^2$ and a list:\n- one\n- two"
nb add graph "sin(x), -5, 5"
nb add math "1/0"
nb add python "s = 'café ≤ √'\ns"
nb add math "solve(x^2=4,x)"
nb add python "x0 = 3"
nb runall
nb run 5
nb run 6
nb run 7
nb run 8
nb run 9
nb run 10
nb new Empty
nb close
usb host on
usb drop "tests/notebooks/From Jupyter.ipynb" mac
wait 1000
usb drop tests/notebooks/handmade.ipynb win as NOTEBOOKS/handmade.ipynb
wait 1000
nb open handmade
nb runall
nb close
usb host off
usb host on
usb ls NOTEBOOKS
'''


def ensure_venv(venv):
    py = os.path.join(venv, "bin", "python")
    if os.path.exists(py):
        r = subprocess.run([py, "-c", "import nbformat, nbconvert"], capture_output=True)
        if r.returncode == 0:
            return py
    try:
        if not os.path.exists(py):
            subprocess.run([sys.executable, "-m", "venv", venv], check=True, capture_output=True, timeout=300)
        subprocess.run([py, "-m", "pip", "install", "-q", "nbformat", "nbconvert"], check=True,
                       capture_output=True, timeout=600)
        subprocess.run([py, "-c", "import nbformat, nbconvert"], check=True, capture_output=True)
        return py
    except Exception as e:  # no network, no pip, no venv module: skip cleanly
        print(f"nb-validate: skipped (cannot set up nbformat in {venv}: {type(e).__name__})")
        return None


CHECK = r'''
import glob, json, os, subprocess, sys
import nbformat
out = sys.argv[1]
files = sorted(glob.glob(os.path.join(out, "*.ipynb")))
assert len(files) >= 4, files
kinds = set()
for f in files:
    nb = nbformat.read(f, as_version=4)
    nbformat.validate(nb)
    assert nb.nbformat == 4 and nb.nbformat_minor == 5, f
    assert nb.metadata.kernelspec.name == "python3", f
    for c in nb.cells:
        assert c.get("id"), (f, "no cell id")
        kinds.add(c.cell_type)
        for o in c.get("outputs", []):
            kinds.add(o.output_type)
            if "image/png" in o.get("data", {}):
                kinds.add("png")
    print(f"valid: {os.path.basename(f)} ({len(nb.cells)} cells)")
for k in ("markdown", "code", "raw", "stream", "execute_result", "error", "display_data", "png"):
    assert k in kinds, f"no {k} anywhere"
every = os.path.join(out, "Every Kind.ipynb")
nb = json.load(open(every))
srcs = ["".join(c["source"]) for c in nb["cells"]]
assert 'calc.eval(r"integrate(x^2,x)")' in srcs[0], srcs[0]
assert nb["cells"][0]["metadata"]["calculator"] == {"input": "integrate(x^2,x)", "kind": "math"}
assert "calc.plot(" in srcs[5] and nb["cells"][5]["metadata"]["calculator"]["kind"] == "graph"
r = subprocess.run([os.path.join(os.path.dirname(sys.executable), "jupyter"), "nbconvert", "--to", "markdown",
                    "--output-dir", os.path.join(out, "md"), every], capture_output=True, text=True)
assert r.returncode == 0, r.stderr
md = open(os.path.join(out, "md", "Every Kind.md")).read()
for want in ("calc.eval", "printed", "ZeroDivisionError", "# Heading", "![png]"):
    assert want in md, f"{want!r} not in the markdown"
print("nbconvert: Every Kind.ipynb -> markdown ok")
# the example notebooks shipped with the Python module (firmware/notebooks, tools/gen_nb_examples.py)
examples = sorted(glob.glob(os.path.join(sys.argv[2], "*.ipynb")))
assert examples, "no example notebooks"
for f in examples:
    nb = nbformat.read(f, as_version=4)
    nbformat.validate(nb)
    assert nb.nbformat == 4 and nb.nbformat_minor == 5, f
    assert nb.metadata.kernelspec.name == "python3", f
    for c in nb.cells:
        assert c.get("id"), (f, "no cell id")
        for o in c.get("outputs", []):
            assert o.output_type != "error", (f, o.get("evalue"))
    print(f"valid example: {os.path.basename(f)} ({len(nb.cells)} cells)")
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("--venv", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for f in os.listdir(a.out):
        if f.endswith(".ipynb"):
            os.remove(os.path.join(a.out, f))
    # the notebooks, made by the simulator, copied off the CALC drive
    names = ["Every Kind", "Empty", "From Jupyter", "handmade"]
    script = SCRIPT + "".join(f'usb get "NOTEBOOKS/{n}.ipynb" "{os.path.join(a.out, n)}.ipynb"\n' for n in names)
    path = os.path.join(a.out, "export.txt")
    with open(path, "w") as f:
        f.write(script)
    r = subprocess.run([a.sim, "--script", path, "--shots", a.out], capture_output=True, text=True, timeout=120)
    errs = [l for l in r.stdout.splitlines() if l.startswith("ERR")]
    if r.returncode or errs:
        print("nb-validate: the simulator failed:", *errs, r.stderr[-2000:], sep="\n")
        return 1
    py = ensure_venv(a.venv)
    if not py:
        return 0
    r = subprocess.run([py, "-c", CHECK, a.out, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                                           "notebooks")], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        print("nb-validate: FAILED\n" + r.stderr[-3000:])
        return 1
    print("nb-validate: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
