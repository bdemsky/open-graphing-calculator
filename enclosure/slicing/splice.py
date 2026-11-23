"""Splice: layers below the first filament change from A (the whole plate, one filament), layers from
it on from B (the cap tops only, cut at ZCUT and sliced on two filaments with PAUSE swaps).
B was sliced ZCUT lower: its moves are raised by ZCUT and shifted in XY onto A's placement.

usage: splice.py A.gcode B.gcode OUT.gcode ZCUT
"""
import re, sys
A, B, OUT, zcut = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
a = open(A).read().split('\n'); b = open(B).read().split('\n')
fmt = lambda z: f"{round(z, 3):g}"


def bounds(lines, zt):
    on = False; xs = []; ys = []; typ = ''
    for l in lines:
        if l.startswith(';Z:'): on = (l[3:] == zt)
        if l.startswith(';TYPE:'): typ = l[6:]
        if l.startswith('G1') and on and typ not in ('Custom', 'Brim', 'Skirt'):
            d = dict(re.findall(r'([XYE])(-?[\d.]+)', l))
            if 'E' in d and float(d['E']) > 0 and 'X' in d: xs.append(float(d['X'])); ys.append(float(d['Y']))
    return min(xs), min(ys), max(xs), max(ys)


# B's first PAUSE: the layer it is in is the splice layer
ip = b.index('PAUSE')
zb = next(b[i][3:] for i in range(ip, 0, -1) if b[i].startswith(';Z:'))
zs = fmt(float(zb) + zcut)
zl = [l[3:] for l in a if l.startswith(';Z:')]
zp = zl[zl.index(zs) - 1]                       # the layer under the splice: align on it
ba, bb = bounds(a, zp), bounds(b, fmt(float(zp) - zcut))
dx, dy = ba[0] - bb[0], ba[1] - bb[1]
print('splice at Z', zs, 'A', [round(v, 2) for v in ba], 'B', [round(v, 2) for v in bb], 'shift', round(dx, 3),
      round(dy, 3), 'size diff', round((ba[2]-ba[0]) - (bb[2]-bb[0]), 3), round((ba[3]-ba[1]) - (bb[3]-bb[1]), 3))
assert abs(dx) < 0.05 and abs(dy) < 0.05, 'A and B are not at the same bed position (slice both with --arrange 0)'
ia = a.index(';Z:' + zs) - 1; ib = b.index(';Z:' + zb) - 1
assert a[ia] == b[ib] == ';LAYER_CHANGE'


def sh(l, xy=True):
    if l.startswith(';Z:'):
        return ';Z:' + fmt(float(l[3:]) + zcut)
    if re.match(r';-?[\d.]+$', l):                       # the slicer's bare layer-height comment
        return ';' + fmt(float(l[1:]) + zcut)
    if not re.match(r'G[0-3] ', l): return l
    code, _, cmt = l.partition(';')
    if xy:
        code = re.sub(r'X(-?[\d.]+)', lambda m: 'X%.3f' % (float(m.group(1)) + dx), code)
        code = re.sub(r'Y(-?[\d.]+)', lambda m: 'Y%.3f' % (float(m.group(1)) + dy), code)
    code = re.sub(r'Z(-?[\d.]+)', lambda m: 'Z%.3f' % (float(m.group(1)) + zcut), code)
    return code + (';' + cmt if cmt else '')


# the end routine keeps its park position in XY, but its Z moves (lift off the print) are raised too
tail_start = next(i for i in range(ib, len(b)) if b[i].startswith(';PRINT_END')) - 2
eb = b.index('; EXECUTABLE_BLOCK_END'); ea = a.index('; EXECUTABLE_BLOCK_END')
# after the end routine: A's summary (time, filament for the whole plate) and settings, not B's
out = a[:ia] + [sh(l) for l in b[ib:tail_start]] + [sh(l, xy=False) for l in b[tail_start:eb + 1]] + a[ea + 1:]
# the printer's RESUME purges 100 mm at the park spot and does not retract again, so the slicer's
# de-retraction after each PAUSE (G1 E2) would land as a blob on the first part printed: drop it
for i in [i for i, l in enumerate(out) if l == 'PAUSE']:
    j = next(j for j in range(i, i + 30) if re.match(r'G1 E[\d.]+ F\d+$', out[j]))
    out[j] = '; ' + out[j] + ' (dropped: RESUME purges)'
open(OUT, 'w').write('\n'.join(out))
print('pauses', sum(1 for l in out if l == 'PAUSE'), 'lines', len(out), 'tail from', tail_start)
