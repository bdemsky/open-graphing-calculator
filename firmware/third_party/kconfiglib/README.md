# kconfiglib (vendored)

Kconfiglib 14.1.0 by Ulf Magnusson, ISC licence (LICENSE.txt), from PyPI
(`kconfiglib-14.1.0-py2.py3-none-any.whl`, sha256
`edcd35a20e7e138a9a9e96149027f805f785e818d2eae400b6fa8b0c8845114a`). Unmodified copies of:

| file | sha256 |
|---|---|
| `kconfiglib.py` | `d81c16ca77a451e52e93c99d39ad9451d645506450766c9a6b7193d55c439103` |
| `menuconfig.py` | `dd5773ad8aabb348201f60ade19059586f4d550129604552b845968214861fe9` |

Pure Python (3.x, standard library only; `menuconfig.py` uses `curses`, which macOS's Python
has), so `make` works without a virtualenv or the Linux kernel's kconfig tools. Used by
`tools/kconfig.py` (the build's `.config` -> `autoconf.h` step, `make menuconfig`,
`make defconfig`, `make savedefconfig`); the option tree is `firmware/Kconfig`, the tiers are
`firmware/configs/*_defconfig`.
