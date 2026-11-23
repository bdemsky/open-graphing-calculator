#!/usr/bin/env python3
"""Verify that changes to C sources are comment-only.

For every .c/.h file changed between BASE (default: HEAD) and the working tree,
strip comments (respecting string and character literals) and all whitespace, and
compare. Any difference in the remaining code is reported and the script fails.

    python3 tools/check_comment_only.py [BASE]
"""
import subprocess
import sys


def strip(src):
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c in '"\'':
            q, j = c, i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(ch for ch in ''.join(out) if not ch.isspace())


def main(base='HEAD'):
    files = subprocess.run(['git', 'diff', '--name-only', base, '--', '*.c', '*.h'],
                           capture_output=True, text=True, check=True).stdout.split()
    bad = []
    for f in files:
        old = subprocess.run(['git', 'show', f'{base}:{f}'], capture_output=True, text=True)
        if old.returncode:
            bad.append(f'{f}: new file (not allowed in a comment-only change)')
            continue
        try:
            new = open(subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True,
                                      text=True).stdout.strip() + '/' + f, errors='replace').read()
        except FileNotFoundError:
            bad.append(f'{f}: deleted')
            continue
        if strip(old.stdout) != strip(new):
            bad.append(f'{f}: code changed')
    print(f'{len(files)} C file(s) changed; {len(bad)} with non-comment changes')
    for b in bad:
        print('  ' + b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
