import os
FW = os.environ.get('AUDIT_FW') or os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
SP = os.environ.get('AUDIT_SCRATCH') or '/tmp/sparkle_audit'
src = open(os.path.join(FW, 'web', 'test', 'run_mac_shots.js')).read()
a = src.index('const SHOTS = {')
b = src.index('};', a) + 2
shots = {'pie_in_all': (32, 0, 1), 'tray_more': (31, 0, 1), 'lshape': (51, 0, 1), 'shape_family': (53, 0, 1),
         'base10_gate': (13, 1, 1), 'coins_hidden': (44, 0, 1), 'graph_key': (62, 0, 1), 'heist_wheels': (16, 0, 1),
         'eggs_ambig': (10, 0, 3), 'mirror_square': (60, 0, 1), 'hop_start': (15, 0, 1), 'times_as': (9, 1, 1)}
lines = ["const SHOTS = {"]
for k, (s, d, r) in shots.items():
    lines.append(f'  {k}: {{dpr: 1, lines: ["mq fun off", "mq new Mia 2", "mq engine stop", "mq seed {r}", '
                 f'"mq skill {s} {d}", "mq step 83"]}},')
lines.append("};")
src = src[:a] + "\n".join(lines) + src[b:]
src = src.replace('const FW = path.resolve(__dirname, "../..");',
                  'const FW = "%s";' % FW)
open(os.path.join(SP, 'mac_shots.js'), 'w').write(src)
