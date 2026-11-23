#!/bin/sh
# Fetch Giac's regression checks (the check/ directory of the source tarball).
#
#   source : https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac/giac_stable.tgz
#            (the old www-fourier.ujf-grenoble.fr host no longer answers)
#   version: giac 2.1.0 (giac_stable.tgz, last-modified 2026-10-13, ~150 MB)
#   licence: GPL-3.0-or-later (COPYING in the tarball); not committed here,
#            fetched at run time.  The intind*/intheb*/intmit lists derive from
#            Rubi (MIT) and Hebisch/12000.org test sets.
#
# Usage: fetch.sh [DEST]   (default: build/suites/src/giac, relative to firmware/cas)
# The tarball is untrusted data: only check/ and COPYING are extracted and
# nothing from it is ever executed.
set -eu
DEST=${1:-build/suites/src/giac}
URL=https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac/giac_stable.tgz
if ls "$DEST"/giac-*/check/testlimit >/dev/null 2>&1; then
    echo "giac: already fetched in $DEST"
    exit 0
fi
mkdir -p "$DEST"
if [ ! -f "$DEST/giac_stable.tgz" ]; then
    curl -fL --retry 3 -o "$DEST/giac_stable.tgz.part" "$URL"
    mv "$DEST/giac_stable.tgz.part" "$DEST/giac_stable.tgz"
fi
# extract only the check directory and the licence
TOP=$(tar tzf "$DEST/giac_stable.tgz" | head -1 | cut -d/ -f1)
tar xzf "$DEST/giac_stable.tgz" -C "$DEST" "$TOP/check" "$TOP/COPYING"
echo "giac: extracted $DEST/$TOP/check"
