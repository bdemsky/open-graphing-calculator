# Graphing calculator firmware + simulator.
#
#   make sim      macOS simulator   -> build/sim/calcsim
#   make fw       STM32H743 image   -> build/fw/calc.{elf,bin,hex,map}, size report
#   make flash-report  flash bytes per module and bank (from the map file)
#   make test     host unit tests + scripted UI tests (screenshots in sim/screenshots/)
#   make gen      regenerate keymap + font tables
#   make manual   user manual docs/MANUAL.md + docs/manual.html from help/*.txt
#   make web      WebAssembly simulator page -> web/dist/calculator.html (needs Emscripten, see README.md)
#   make web-test run the WASM build under node and compare screenshots with the native simulator
#   make web-image re-render the calculator picture + key map from the enclosure CAD (needs ../.venv)
#   make sparkle-web  the standalone Sparkle Town page -> web/dist/sparkle.html (full window, high
#                 resolution, sound; Emscripten); sparkle-test tests it under node; sparkle-art
#                 regenerates its high-resolution art (tools/gen_mq_art_hires.py, needs ../.venv),
#                 sparkle-vector its vector art (tools/gen_mq_vector.py -> modules/mathquest/art_vector)
#   make all      all of the above
#   make modules  QSPI content modules of this configuration -> build/modules/*.MOD, INDEX.TXT,
#                 calc_qspi.bin (store image: OpenOCD / simulator factory content)
#   make modx     code module images (modules/*: position-independent ARM code, app/modx.h)
#   make menuconfig | defconfig TIER=x | savedefconfig    build configuration (Kconfig, see below)
#
# Configuration (Kconfig, firmware/Kconfig; docs/QSPI_CONTENT.md): every build directory has its
# own $(BUILD)/.config, created from configs/standard_defconfig unless TIER= picks another one:
#   TIER=core       everything in internal flash (rollback; works with a dead QSPI, USB DFU only)
#   TIER=standard   core CAS in internal flash, full manual as a QSPI module (default)
#   TIER=advanced   standard + CAS extension modules in the QSPI (phase 2)
# TIER=x re-applies configs/x_defconfig; without TIER the existing .config (e.g. from
# make menuconfig) is kept. CAS=, CONTENT=internal|qspi and MANUAL=full|essentials on the
# command line override the matching options and are saved into the .config.
#
# CAS selection (see README.md):
#   CAS=src (default)   compile the real library's sources from cas/src/ for each target
#   CAS=stub            numeric stand-in in cas_stub/ (tests/unit/test_cas_stub always uses it)
#   CAS=lib             link prebuilt archives CAS_LIB_HOST / CAS_LIB_FW
#   CAS=auto            'src' if cas/src/ contains .c files, else 'stub'

ARM_BIN    ?= /Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin
ARM_PREFIX ?= $(ARM_BIN)/arm-none-eabi-
ARM_CC      = $(ARM_PREFIX)gcc
ARM_OBJCOPY = $(ARM_PREFIX)objcopy
ARM_SIZE    = $(ARM_PREFIX)size
HOSTCC     ?= clang
PYTHON     ?= python3
SAN        ?= 1
CAS        ?= src
CAS_DIR    ?= cas
CAS_LIB_HOST ?= $(CAS_DIR)/build/host/libcas.a
CAS_LIB_FW   ?= $(CAS_DIR)/build/arm/libcas.a

# A tier other than the default builds in its own directory, so `make fw TIER=core` never
# changes the default build's .config (an explicit BUILD= still wins).
BUILD := $(if $(filter-out standard,$(TIER)),build/tier-$(TIER),build)
# Versions (README.md "Versions and releases"): versions.txt holds the release version, one for the
# firmware and every module of a release (`release X.Y.Z`); a build that is not `make release` (FW_RELEASE=1) is named
# "X.Y.Z+N-gHASH[-modified]" by tools/release_version.py.
VERSIONS := versions.txt
RELEASE_VERSION := $(shell awk '$$1 == "release" { print $$2 }' $(VERSIONS))
FW_VERSION := $(RELEASE_VERSION)
FW_VERSION_STR := $(shell $(PYTHON) tools/release_version.py describe $(if $(FW_RELEASE),--release))
# the module header's 24-byte "built with" field
MOD_BUILT_WITH = $(subst -modified,*,$(FW_VERSION_STR))

CAS_CMDLINE := $(if $(filter command line,$(origin CAS)),$(strip $(CAS)))
ifeq ($(CAS),auto)
  ifneq ($(strip $(shell find $(CAS_DIR)/src -name '*.c' 2>/dev/null | head -1)),)
    override CAS := src
  else
    override CAS := stub
  endif
endif

# ------------------------------------------------------------------ configuration (Kconfig)
# tools/kconfig.py sync runs on every make invocation: it creates or updates $(BUILD)/.config,
# $(BUILD)/gen/autoconf.h (-include'd by every C file) and $(BUILD)/gen/config_gen.c, touching
# them only when they change.
TIER ?=
KCONFIG_DEFCONFIG := configs/$(if $(TIER),$(TIER),standard)_defconfig
upper = $(shell echo '$(1)' | tr a-z A-Z)
KCONFIG_SET :=
ifneq ($(CAS_CMDLINE),)
  KCONFIG_SET += --set CAS_$(call upper,$(CAS))=y
endif
ifneq ($(strip $(CONTENT)),)
  KCONFIG_SET += --set CONTENT_$(call upper,$(CONTENT))=y
endif
ifneq ($(strip $(MANUAL)),)
  KCONFIG_SET += --set MANUAL_$(call upper,$(MANUAL))=y
endif
KCONFIG_OUT := $(shell $(PYTHON) tools/kconfig.py sync --config $(BUILD)/.config --gen-dir $(BUILD)/gen \
	--defconfig $(KCONFIG_DEFCONFIG) $(if $(TIER),--force) $(KCONFIG_SET) 2>&1 || echo KCONFIG_FAILED)
ifneq ($(findstring KCONFIG_FAILED,$(KCONFIG_OUT)),)
  $(error $(subst KCONFIG_FAILED,,$(KCONFIG_OUT)))
endif
ifneq ($(strip $(KCONFIG_OUT)),)
  $(info $(KCONFIG_OUT))
endif
include $(BUILD)/.config
override CAS := $(if $(CONFIG_CAS_STUB),stub,$(if $(CONFIG_CAS_LIB),lib,src))
CONTENT_MODE := $(if $(CONFIG_CONTENT_INTERNAL),internal,qspi)
AUTOCONF := $(BUILD)/gen/autoconf.h
GEN_CONFIG := $(BUILD)/gen/config_gen.c
# Extension modules are placeholders until their sources exist (phase 2).
CASX_SELECTED := $(foreach v,$(filter CONFIG_CASX_%,$(.VARIABLES)),$(if $(filter y,$($(v))),$(v)))
ifneq ($(strip $(CASX_SELECTED)),)
  $(error $(CASX_SELECTED): CAS extension module(s) selected that have no sources yet (phase 2, \
	docs/QSPI_CONTENT.md); turn them off in make menuconfig)
endif

# ------------------------------------------------------------------ build version
# core/version.h: the version string and the time of the link, in a source generated by every
# link rule (tools/gen_version.py). Each target's version object depends on all its other objects
# and on VERSION_STAMP (rewritten here only when the version string changes: a commit, a change to
# the tree, a release), so it is rebuilt exactly when that target is linked again: the `version`
# command, About and INFO.TXT name the build that runs.
VERSION_STAMP := $(BUILD)/gen/version_stamp
ifneq ($(shell cat $(VERSION_STAMP) 2>/dev/null),$(FW_VERSION_STR))
  $(shell mkdir -p $(BUILD)/gen && echo '$(FW_VERSION_STR)' > $(VERSION_STAMP))
endif
# $(call gen_version,COMPILER AND FLAGS): the recipe of a version object $@
define gen_version
	@$(PYTHON) tools/gen_version.py --version '$(FW_VERSION_STR)' --out $(@:.o=.c)
	$(1) -c $(@:.o=.c) -o $@
endef

# ------------------------------------------------------------------ sources
GEN_KEYMAP := core/gen/keymap_gen.c core/gen/keymap_gen.h
GEN_FONTS  := core/gen/fonts_gen.c
# Help manual: help/*.txt -> compressed tables (tools/gen_help.py), generated per build directory.
# CONTENT_INTERNAL + MANUAL_FULL: the whole manual in flash; otherwise flash keeps the @essentials
# pages, and with CONTENT_QSPI + MANUAL_FULL the full manual becomes the "manual" module.
HELP_TXT := $(sort $(wildcard help/*.txt))
GEN_HELP := $(BUILD)/gen/help_gen.c
HELP_MODE := $(if $(and $(CONFIG_CONTENT_INTERNAL),$(CONFIG_MANUAL_FULL)),internal,qspi)
HELP_ABI := 1 # manual payload format: keep equal to HELP_ABI in tools/gen_help.py and app/help.c
MODULES_DIR := $(BUILD)/modules
MANUAL_PAYLOAD := $(if $(and $(CONFIG_CONTENT_QSPI),$(CONFIG_MANUAL_FULL)),$(BUILD)/gen/manual.bin)
# Release file names carry the version in 8.3 form (tools/mkmodule.py filename: manual 0.1.0 ->
# MANUA010.MOD), so several versions sit side by side on the microSD card.
MANUAL_VERSION := $(RELEASE_VERSION)
MANUAL_MOD := $(if $(MANUAL_PAYLOAD),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename manual $(MANUAL_VERSION)))
# Code modules (app/modx.h, docs/QSPI_CONTENT.md 12): the Animation module is built in every QSPI
# tier (the firmware can install it from the microSD card); MOD_ANIMATION puts it into the store
# image too (the factory content of the advanced tier). modx ABI: keep equal to MODX_ABI in app/modx.h.
MODX_ABI := 1
ANIM_VERSION := $(RELEASE_VERSION)
MODX_OBJDIR := $(BUILD)/modx
ANIM_SRC := $(sort $(wildcard modules/animation/*.c)) modules/modx_rt.c
ANIM_ELF := $(MODX_OBJDIR)/animation.elf
ANIM_BIN := $(MODX_OBJDIR)/animation.bin
ANIM_MOD := $(if $(CONFIG_CONTENT_QSPI),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename animation $(ANIM_VERSION)))
# Math Quest (modules/mathquest, docs/MATHQUEST.md) is built like Animation but is NOT part of the
# release library: `make mathquest-module` builds it, MOD_MATHQUEST puts it into the store image,
# and the simulators carry it as an "extra" library file (console `module library extra`).
MQ_VERSION := $(RELEASE_VERSION)
MQ_SRC := $(sort $(wildcard modules/mathquest/*.c)) modules/modx_rt.c
MQ_ELF := $(MODX_OBJDIR)/mathquest.elf
MQ_BIN := $(MODX_OBJDIR)/mathquest.bin
MQ_MOD := $(if $(CONFIG_CONTENT_QSPI),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename mathquest $(MQ_VERSION)))
# The advanced CAS extension (modules/casx + cas/ext/adv, cas/src/casx.h): a code module in the QSPI
# tiers (built for the release library; MOD_CASX puts it into the store image), linked into the
# firmware in the core tier and into every host build (simulators, web), where it still binds
# through the module store (QSPI tiers) or registers at the first use (core tier).
CASX_VERSION := $(RELEASE_VERSION)
CAS_EXT_SRC := $(sort $(wildcard $(CAS_DIR)/ext/*/*.c))
CASX_SRC := $(sort $(wildcard modules/casx/*.c)) $(CAS_EXT_SRC) modules/modx_rt.c
CASX_ELF := $(MODX_OBJDIR)/casx.elf
CASX_BIN := $(MODX_OBJDIR)/casx.bin
CASX_MOD := $(if $(and $(CONFIG_CONTENT_QSPI),$(CONFIG_CAS_SRC)),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename casx $(CASX_VERSION)))
# The Geometry app (modules/geometry, app/geo_core.h): a code module in the QSPI tiers (release
# library; MOD_GEOMETRY puts it into the store image), linked into flash bank 2 in the core tier
# (GEOMETRY_BUILTIN), linked into every host build like the other modules.
GEO_VERSION := $(RELEASE_VERSION)
GEO_MOD_SRC := $(sort $(wildcard modules/geometry/*.c))
GEO_SRC := $(GEO_MOD_SRC) modules/modx_rt.c
GEO_ELF := $(MODX_OBJDIR)/geometry.elf
GEO_BIN := $(MODX_OBJDIR)/geometry.bin
GEO_MOD := $(if $(CONFIG_CONTENT_QSPI),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename geometry $(GEO_VERSION)))
# Python (modules/python + third_party/micropython, app/py_core.h): the first code-load module
# (copied into an executable SDRAM area and relocated when it binds, modules/modx_load.ld,
# tools/modreloc.py), built in every QSPI tier; MOD_PYTHON puts it into the store image. The
# simulators link the same sources (not the web page: its GC cannot see the wasm stack). MicroPython's
# generated headers (qstrs, module and root-pointer tables) come from tools/py_genhdr.py.
PY_VERSION := $(RELEASE_VERSION)
MPY_TOP := third_party/micropython
MPY_GEN := $(BUILD)/gen/mpy
MPY_STAMP := $(MPY_GEN)/stamp
PY_MOD_SRC := $(sort $(wildcard modules/python/*.c))
MPY_SRC := $(sort $(wildcard $(MPY_TOP)/py/*.c)) $(addprefix $(MPY_TOP)/extmod/,modheapq.c modjson.c modrandom.c \
	modre.c modtime.c) $(MPY_TOP)/shared/runtime/gchelper_generic.c
# core sources the module's image compiles too (it reaches no core code but through its API tables);
# the host builds link the core's copy
PY_CORE_SHARED := app/png.c core/rle.c
PY_SRC := $(PY_MOD_SRC) $(PY_CORE_SHARED) $(MPY_SRC)
PY_HOST_SRC := $(if $(CONFIG_PYTHON),$(filter-out $(PY_CORE_SHARED),$(PY_SRC)))
MPY_INC := -Imodules/python -I$(MPY_TOP) -I$(MPY_GEN)
# MicroPython's own warning set is looser than the firmware's (vendored, unmodified)
MPY_WARN := -std=gnu11 -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
	-Wno-unused-function $(MPY_INC)
PY_ELF := $(MODX_OBJDIR)/python.elf
PY_BIN := $(MODX_OBJDIR)/python.bin
PY_MOD := $(if $(CONFIG_PYTHON),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename python $(PY_VERSION)))
# The browser notebook (web/notebook, README.md "Browser notebook"): one self-contained HTML page
# (KaTeX inlined) built by web/notebook/build.py (Python only, byte-for-byte reproducible); the
# "webui" data module carries it, and the CALC drive serves it as CALCULATOR.HTML (QSPI tiers).
NB_DIR := web/notebook
NB_HTML := $(BUILD)/gen/calculator_notebook.html
NB_INPUTS := $(NB_DIR)/build.py $(sort $(wildcard $(NB_DIR)/src/*)) keys/layout.py help/python.txt third_party/katex/LICENSE \
	third_party/katex/katex.min.js third_party/katex/katex.min.css $(sort $(wildcard third_party/katex/fonts/*.woff2))
WEBUI_ABI := 1 # keep equal to the "webui" entry of core_ifaces in app/modules_ui.c
WEBUI_VERSION := $(RELEASE_VERSION)
WEBUI_MOD := $(if $(CONFIG_CONTENT_QSPI),$(MODULES_DIR)/$(shell $(PYTHON) tools/mkmodule.py filename webui $(WEBUI_VERSION)))
# the release library (microSD MODULES/ + INDEX.TXT) and the factory store image
MODULE_FILES := $(strip $(MANUAL_MOD) $(ANIM_MOD) $(CASX_MOD) $(GEO_MOD) $(PY_MOD) $(WEBUI_MOD))
STORE_MODS := $(strip $(MANUAL_MOD) $(if $(CONFIG_MOD_ANIMATION),$(ANIM_MOD)) $(if $(CONFIG_MOD_MATHQUEST),$(MQ_MOD)) \
	$(if $(CONFIG_MOD_CASX),$(CASX_MOD)) $(if $(CONFIG_MOD_GEOMETRY),$(GEO_MOD)) $(if $(CONFIG_MOD_PYTHON),$(PY_MOD)) \
	$(if $(CONFIG_MOD_WEBUI),$(WEBUI_MOD)))
STORE_BIN := $(MODULES_DIR)/calc_qspi.bin
MODULE_INDEX := $(MODULES_DIR)/INDEX.TXT
CONTENT_C := $(BUILD)/gen/content_img.c
HELP_EXAMPLES := $(BUILD)/gen/help_examples.txt
# the help page "keys" is generated from the keyboard layout (keys/layout.py)
HELP_KEYS := $(BUILD)/gen/help_keys.txt
CORE_SRC := $(sort $(wildcard core/*.c)) core/gen/keymap_gen.c core/gen/fonts_gen.c
# code module sources: linked into the simulators and tests (same API table and binding path as the
# hardware), built as separate position-independent images for the firmware (make modx)
MODX_HOST_SRC := $(filter-out modules/python/%,$(sort $(wildcard modules/*/*.c)))
APP_SRC  := $(sort $(wildcard app/*.c)) $(GEN_HELP) $(GEN_CONFIG)
# device models shared by the two simulators (fault injection, factory content)
HOSTDEV_SRC := platform/hostdev/hostdev.c platform/hostdev/usbhost.c $(CONTENT_C)
SIM_SRC  := $(sort $(wildcard platform/sim/*.c)) $(HOSTDEV_SRC)
SIM_MAIN := platform/sim/sim_main.c
FW_SRC   := $(sort $(wildcard platform/stm32h743/*.c))

STUB_SRC := $(sort $(wildcard cas_stub/*.c))
ifeq ($(CAS),stub)
  CAS_SRC := $(STUB_SRC)
  CAS_LIBS_HOST :=
  CAS_LIBS_FW :=
else ifeq ($(CAS),src)
  CAS_SRC := $(sort $(shell find $(CAS_DIR)/src -name '*.c'))
  CAS_HOST_EXT := $(CAS_EXT_SRC)
  CAS_LIBS_HOST :=
  CAS_LIBS_FW :=
else ifeq ($(CAS),lib)
  CAS_SRC :=
  CAS_LIBS_HOST := $(CAS_LIB_HOST)
  CAS_LIBS_FW := $(CAS_LIB_FW)
else
  $(error CAS must be stub, src, lib or auto)
endif

INCLUDES := -Iinclude -Icore -Iapp -I$(CAS_DIR)/include
DEFS := -DCAS_IMPL=\"$(CAS)\" -include $(AUTOCONF)
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CWARN = $(WARN)
# The real CAS library is built with its own (looser) warning set.
CAS_WARN := -Wall -Wno-unused-parameter

# ------------------------------------------------------------------ host
HOST_CFLAGS = -std=c11 -g -O1 $(CWARN) $(INCLUDES) -Iplatform/hostdev $(DEFS) -DPLATFORM_SIM=1 -D_DARWIN_C_SOURCE
ifeq ($(SAN),1)
  HOST_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
  HOST_LDFLAGS += -fsanitize=address,undefined
endif
HOST_OBJDIR := $(BUILD)/host
host_obj = $(patsubst %.c,$(HOST_OBJDIR)/%.o,$(1))

SIM_LIB_SRC := $(CORE_SRC) $(APP_SRC) $(MODX_HOST_SRC) $(PY_HOST_SRC) $(CAS_SRC) $(CAS_HOST_EXT) \
	$(filter-out $(SIM_MAIN),$(SIM_SRC))
SIM_LIB_OBJ := $(call host_obj,$(SIM_LIB_SRC))

$(HOST_OBJDIR)/%.o: %.c $(GEN_KEYMAP) $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c $< -o $@

# MicroPython's generated headers, shared by the host and ARM builds
$(MPY_STAMP): $(PY_SRC) $(wildcard modules/python/*.h) $(AUTOCONF) tools/py_genhdr.py
	$(PYTHON) tools/py_genhdr.py --cc $(HOSTCC) --top $(MPY_TOP) --out $(MPY_GEN) \
		--flags "$(INCLUDES) $(MPY_INC) -include $(AUTOCONF)" $(PY_SRC)
$(call host_obj,$(PY_SRC)): CWARN = $(MPY_WARN)
# the VM moves its instruction pointer by unsigned offsets (wrapping on purpose)
$(call host_obj,$(PY_SRC)): HOST_CFLAGS += -fno-sanitize=pointer-overflow
$(call host_obj,$(PY_SRC)): | $(MPY_STAMP)
# the collector reads the C stack word by word (conservative roots): not through ASan's redzones
$(call host_obj,$(MPY_TOP)/py/gc.c $(MPY_TOP)/shared/runtime/gchelper_generic.c): HOST_CFLAGS += -fno-sanitize=address

$(call host_obj,$(filter-out $(STUB_SRC),$(CAS_SRC))): CWARN = $(CAS_WARN)
$(call host_obj,$(CAS_EXT_SRC)): CWARN = $(CAS_WARN) -I$(CAS_DIR)/src -I$(CAS_DIR)/ext

HOST_VERSION := $(HOST_OBJDIR)/version.o
$(HOST_VERSION): $(SIM_LIB_OBJ) $(call host_obj,$(SIM_MAIN)) $(VERSION_STAMP) tools/gen_version.py
	$(call gen_version,$(HOSTCC) $(HOST_CFLAGS))

$(BUILD)/sim/calcsim: $(SIM_LIB_OBJ) $(call host_obj,$(SIM_MAIN)) $(HOST_VERSION) $(CAS_LIBS_HOST)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^ -lm

.PHONY: sim
sim: $(BUILD)/sim/calcsim
	@echo "simulator: $< (CAS=$(CAS))"

# ------------------------------------------------------------------ tests
TEST_SRC := $(sort $(wildcard tests/unit/test_*.c))
TEST_BIN := $(patsubst tests/unit/%.c,$(BUILD)/test/%,$(TEST_SRC))

$(BUILD)/test/%: $(HOST_OBJDIR)/tests/unit/%.o $(SIM_LIB_OBJ) $(HOST_VERSION) $(CAS_LIBS_HOST)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^ -lm

# The stub's own unit test always links the stub, whatever CAS the build uses.
STUB_LIB_OBJ := $(call host_obj,$(CORE_SRC) $(APP_SRC) $(MODX_HOST_SRC) $(PY_HOST_SRC) $(STUB_SRC) \
	$(filter-out $(SIM_MAIN),$(SIM_SRC)))
$(BUILD)/test/test_cas_stub: $(HOST_OBJDIR)/tests/unit/test_cas_stub.o $(STUB_LIB_OBJ) $(HOST_VERSION)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^ -lm

.PHONY: test unit-test ui-test help-test config-check tier-test upload-test
# `make -jN test` builds in parallel, then runs the tests one after the other: several of them share
# the simulator's outputs (sim/screenshots, build/ui_notebook.html, build/nbcheck, the notebook page),
# and a test running while another target relinks the simulator saw a half-written binary.
TEST_TARGETS := unit-test ui-test nbhtml-check help-test printf-check keydoc-check manual-check config-check \
	upload-test mq-tools-test version-test mq-mark-check python-test nb-validate notebook-test tier-test \
	nb-examples-check
test: $(BUILD)/sim/calcsim $(TEST_BIN) $(HELP_EXAMPLES) $(MODULE_FILES) $(NB_HTML)
	@$(MAKE) --no-print-directory -j1 $(TEST_TARGETS)

# The browser notebook (web/notebook): its unit tests and its console protocol against this
# simulator, under node (no browser); an exported notebook is validated with nbformat when
# $(NB_VENV) has it (nb-validate makes it), and the notebooks nb-validate exported from the calculator's
# Notebook app ($(BUILD)/nbcheck) must open. Skipped without node.
NB_NODE ?= node
.PHONY: notebook notebook-test
notebook: $(NB_HTML)
	@ls -l $(NB_HTML)
notebook-test: $(NB_HTML) $(BUILD)/sim/calcsim | nb-validate
	@if command -v $(NB_NODE) >/dev/null 2>&1; then \
		CALCSIM=$(BUILD)/sim/calcsim NB_HTML=$(NB_HTML) NB_OUT=$(BUILD)/gen/notebook-test \
		NB_PYTHON=$(NB_VENV)/bin/python NB_DEVICE_DIR=$(BUILD)/nbcheck NB_PY_MODULE=$(if $(CONFIG_MOD_PYTHON),1,0) \
		NB_WEBUI=$(if $(CONFIG_MOD_WEBUI),1,0) $(NB_NODE) --test --test-concurrency=1 $(NB_DIR)/test/*.test.js; \
	else echo "notebook-test: node not found, skipped (the page needs a manual check in Chrome)"; fi

# Python (modules/python): a subset of MicroPython's own tests/basics and the calc library's tests
# (those need the real CAS), run in the simulator's interpreter (tests/run_python.py)
.PHONY: python-test
python-test: $(BUILD)/sim/calcsim
	@if [ -n "$(CONFIG_MOD_PYTHON)" ]; then $(PYTHON) tests/run_python.py --sim $(BUILD)/sim/calcsim \
		tests/python/basics/*.py $(if $(filter src,$(CAS)),tests/python/test_*.py); \
		else echo "python-test: the Python module is not in this configuration"; fi

# Example notebooks (firmware/notebooks/*.ipynb, docs/NOTEBOOK.md "Example notebooks"), built into the
# Python module as modules/python/nb_examples.c (checked in): nb-examples runs every example on the
# simulator, stores the outputs in the .ipynb files and writes the C file; nb-examples-c only writes the
# C file; nb-examples-check (make test) fails when the C file is not that of the notebooks.
.PHONY: nb-examples nb-examples-c nb-examples-check
nb-examples: $(BUILD)/sim/calcsim
	$(PYTHON) tools/gen_nb_examples.py --refresh --sim $(BUILD)/sim/calcsim --c-out modules/python/nb_examples.c
nb-examples-c:
	$(PYTHON) tools/gen_nb_examples.py --c-out modules/python/nb_examples.c
nb-examples-check:
	$(PYTHON) tools/gen_nb_examples.py --check modules/python/nb_examples.c

# Notebooks (modules/python nb_*.c) exported by the simulator, checked with Jupyter's nbformat package and
# converted by `jupyter nbconvert` (tools/nb_validate.py; a virtualenv in NB_VENV made on first use, the
# check skipped when it cannot be made)
NB_VENV ?= $(BUILD)/nbvenv
.PHONY: nb-validate
nb-validate: $(BUILD)/sim/calcsim
	@if [ -n "$(CONFIG_MOD_PYTHON)" ]; then $(PYTHON) tools/nb_validate.py --sim $(BUILD)/sim/calcsim \
		--venv $(NB_VENV) --out $(BUILD)/nbcheck; else echo "nb-validate: the Python module is not in this configuration"; fi

# Sparkle Town's time marks go through mq_mark / mq_mark_now (modules/mathquest/mq.h; CODE_REVIEW_2.md B2, CR2-05):
# `anim_t | 1` was a moment in the future on an even tick.
.PHONY: mq-mark-check
mq-mark-check:
	@! grep -n -E '(anim_t|->now) \| 1\b' modules/mathquest/*.c || { echo "mq-mark-check: use mq_mark(q) / mq_mark_now(q)"; exit 1; }
	@echo "mq-mark-check: no | 1 time marks"

# Math Quest tools: the built-in art is up to date (tools/gen_mq_art.py) and the theme-pack
# builder round-trips its format (tools/make_theme.py --selftest).
.PHONY: mq-art mq-tools-test
mq-art:
	$(PYTHON) tools/gen_mq_art.py --c modules/mathquest/mq_art.c
	$(PYTHON) tools/gen_mq_items.py --c modules/mathquest/mq_items.c --h modules/mathquest/mq_items.h
mq-tools-test: $(BUILD)/test/test_mathquest
	$(PYTHON) tools/gen_mq_art.py --check modules/mathquest/mq_art.c
	$(PYTHON) tools/gen_mq_items.py --check-c modules/mathquest/mq_items.c --check-h modules/mathquest/mq_items.h
	$(PYTHON) tools/make_theme.py --selftest
	$(PYTHON) tools/gen_mq_art.py --sheet $(BUILD)/gen/mqtheme
	$(PYTHON) tools/make_theme.py --sheet $(BUILD)/gen/mqtheme/sheet.png --manifest $(BUILD)/gen/mqtheme/theme.json \
		--out $(BUILD)/gen/mqtheme/PASTEL.THM
	$(PYTHON) tools/gen_mq_items.py --sheet $(BUILD)/gen/mqtheme
	$(PYTHON) tools/make_theme.py --sheet $(BUILD)/gen/mqtheme/items.png --manifest $(BUILD)/gen/mqtheme/items.json \
		--out $(BUILD)/gen/mqtheme/ITEMS.THM
	$(PYTHON) tools/make_theme.py --sheet $(BUILD)/gen/mqtheme/sheet.png --manifest tests/unit/mq_chars.json \
		--out $(BUILD)/gen/mqtheme/CHARS.THM
	MQ_SAMPLE_THM=$(BUILD)/gen/mqtheme/PASTEL.THM MQ_SAMPLE_ITEMS=$(BUILD)/gen/mqtheme/ITEMS.THM \
		MQ_SAMPLE_CHARS=$(BUILD)/gen/mqtheme/CHARS.THM $(BUILD)/test/test_mathquest

# README key table and keys/keyboard.svg from keys/layout.py
.PHONY: keydoc keydoc-check
keydoc:
	$(PYTHON) tools/gen_keydoc.py --readme --svg
keydoc-check:
	$(PYTHON) tools/gen_keydoc.py --check

# User manual docs/MANUAL.md + docs/manual.html from help/*.txt (the built-in help)
.PHONY: manual manual-check
manual: $(HELP_KEYS)
	$(PYTHON) tools/gen_manual.py --extra $(HELP_KEYS)
manual-check: $(HELP_KEYS)
	$(PYTHON) tools/gen_manual.py --extra $(HELP_KEYS) --check

unit-test: $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; $$t; done
	@echo "unit tests passed"

UI_SHOTS ?= sim/screenshots
ui-test: $(BUILD)/sim/calcsim
	$(PYTHON) tests/run_ui.py --sim $(BUILD)/sim/calcsim --shots $(UI_SHOTS) tests/ui/*.txt

# NOTEBOOK.HTML of the CALC drive, as tests/ui/114_notebook_html.txt fetched it: no scripts, nothing from
# elsewhere, every picture a PNG that decodes (app/nbhtml.c)
.PHONY: nbhtml-check
nbhtml-check: ui-test
	$(PYTHON) tools/nbhtml_check.py build/ui_notebook.html

# The release version logic (tools/release_version.py): no change -> no bump, a changed module payload
# -> that module's PATCH, changed firmware images -> the firmware's PATCH, hand bumps kept.
.PHONY: version-test
version-test:
	$(PYTHON) tools/release_version.py selftest

# Every tier's defconfig still loads cleanly (no unknown or unsettable options).
config-check:
	$(PYTHON) tools/kconfig.py check configs/*_defconfig

# newlib-nano (the firmware's libc) prints no long long: "%lld" comes out as "ld"
.PHONY: printf-check
printf-check:
	@if grep -rn -E '%[-+ #0-9.*]*ll[diouxX]|PRI[diouxX]64' --include='*.c' --include='*.h' \
		app cas/src cas/ext core include modules platform 2>/dev/null; then \
		echo "printf-check: newlib-nano has no %ll; cast to long or format by hand"; exit 1; fi

# The other tiers (the default build directory is the standard tier): each gets its own build
# directory, its simulator runs every UI test, and its firmware links within the flash budget
# (when the ARM toolchain is installed). CAS= drops a command-line CAS for these builds.
TIER_TESTS ?= core advanced
tier-test:
	@set -e; for t in $(TIER_TESTS); do \
		echo "== tier $$t"; \
		$(MAKE) --no-print-directory BUILD=$(BUILD)/tier-$$t TIER=$$t CAS= CONTENT= MANUAL= sim ui-test \
			UI_SHOTS=$(BUILD)/tier-$$t/shots; \
		if [ -x "$(ARM_CC)" ]; then $(MAKE) --no-print-directory BUILD=$(BUILD)/tier-$$t TIER=$$t CAS= \
			CONTENT= MANUAL= fw > $(BUILD)/tier-$$t/fw.log 2>&1 || { tail -30 $(BUILD)/tier-$$t/fw.log; exit 1; }; \
			grep "^flash bank" $(BUILD)/tier-$$t/fw.log; fi; \
	done

# The console upload path end to end: an empty simulator gets the manual module through
# tools/calcctl.py --upload-module (the way it works over the UART on hardware).
upload-test: $(BUILD)/sim/calcsim $(MODULE_FILES)
	@if [ -n "$(MANUAL_MOD)" ]; then $(PYTHON) tests/run_upload.py --sim $(BUILD)/sim/calcsim $(MANUAL_MOD); \
		else echo "upload-test: no modules in this configuration ($(CONTENT_MODE))"; fi

# Every help example with a `result:` line, checked against the linked CAS
# (lines are `ifcas src ...`, so the stub build only checks that the pages load).
help-test: $(BUILD)/sim/calcsim $(HELP_EXAMPLES)
	$(PYTHON) tests/run_ui.py --sim $(BUILD)/sim/calcsim --shots $(BUILD)/gen $(HELP_EXAMPLES)

$(HELP_KEYS): tools/gen_keydoc.py keys/layout.py
	$(PYTHON) tools/gen_keydoc.py --help-page $@
$(GEN_HELP): tools/gen_help.py tools/gen_font.py keys/layout.py $(HELP_TXT) $(HELP_KEYS) $(AUTOCONF)
	$(PYTHON) tools/gen_help.py --out-c $@ --examples $(HELP_EXAMPLES) --extra $(HELP_KEYS) \
		--content $(HELP_MODE) $(if $(MANUAL_PAYLOAD),--out-module-payload $(MANUAL_PAYLOAD))
$(HELP_EXAMPLES): $(GEN_HELP)
ifneq ($(MANUAL_PAYLOAD),)
$(MANUAL_PAYLOAD): $(GEN_HELP)
endif

# ------------------------------------------------------------------ content modules
# One file per module (+ INDEX.TXT) for the microSD module library and the console upload, and
# the store image calc_qspi.bin with all of them (OpenOCD to 0x90000000, and as content_img.c the
# simulators' factory content).
$(MANUAL_MOD): $(MANUAL_PAYLOAD) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(MANUAL_PAYLOAD) --name manual \
		--version $(MANUAL_VERSION) --iface help --abi $(strip $(HELP_ABI)) \
		--tier standard --kconfig MANUAL_FULL --built-with "$(MOD_BUILT_WITH)" --out $@
$(ANIM_MOD): $(ANIM_BIN) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(ANIM_BIN) --name animation --version $(ANIM_VERSION) \
		--iface modx --abi $(MODX_ABI) --kind code-xip --tier advanced --kconfig MOD_ANIMATION \
		--built-with "$(MOD_BUILT_WITH)" --out $@
$(CASX_MOD): $(CASX_BIN) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(CASX_BIN) --name casx --version $(CASX_VERSION) \
		--iface modx --abi $(MODX_ABI) --kind code-xip --tier advanced --kconfig MOD_CASX \
		--built-with "$(MOD_BUILT_WITH)" --out $@
$(GEO_MOD): $(GEO_BIN) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(GEO_BIN) --name geometry --version $(GEO_VERSION) \
		--iface modx --abi $(MODX_ABI) --kind code-xip --tier standard --kconfig MOD_GEOMETRY \
		--built-with "$(MOD_BUILT_WITH)" --out $@
$(MQ_MOD): $(MQ_BIN) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(MQ_BIN) --name mathquest --version $(MQ_VERSION) \
		--iface modx --abi $(MODX_ABI) --kind code-xip --tier advanced --kconfig MOD_MATHQUEST \
		--built-with "$(MOD_BUILT_WITH)" --out $@
$(NB_HTML): $(NB_INPUTS)
	$(PYTHON) $(NB_DIR)/build.py --out $@
$(WEBUI_MOD): $(NB_HTML) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(NB_HTML) --name webui --version $(WEBUI_VERSION) \
		--iface webui --abi $(strip $(WEBUI_ABI)) --tier standard --kconfig MOD_WEBUI \
		--built-with "$(MOD_BUILT_WITH)" --out $@
$(CONTENT_C): $(MODULE_FILES) $(MQ_MOD) tools/mkmodule.py $(AUTOCONF) $(VERSIONS)
	$(PYTHON) tools/mkmodule.py store --out $(STORE_BIN) --c-out $@ $(STORE_MODS) $(if $(MODULE_FILES),--library $(MODULE_FILES)) $(if $(MQ_MOD),--extra $(MQ_MOD))
$(STORE_BIN): $(CONTENT_C)
$(MODULE_INDEX): $(MODULE_FILES) tools/mkmodule.py $(VERSIONS)
	@mkdir -p $(dir $@)
	$(if $(MODULE_FILES),$(PYTHON) tools/mkmodule.py index --out $@ $(MODULE_FILES),echo "# no modules" > $@)

.PHONY: modules
modules: $(STORE_BIN) $(MODULE_INDEX)
	@$(if $(MODULE_FILES),$(PYTHON) tools/mkmodule.py check $(MODULE_FILES),echo "no modules (CONTENT=$(CONTENT_MODE))")

# ------------------------------------------------------------------ code module images
# Each module directory modules/NAME/ becomes one position-independent image (app/modx.h):
# compiled with -fPIE and hidden visibility (PC-relative access to its own code and constants),
# linked at address 0 by modules/modx_image.ld (descriptor first; .data/.bss/GOT refused) with -q
# so tools/modlink.py can check that no absolute relocation is left but the descriptor's, then
# objcopy'd to the raw payload. No C library: the module calls the core through the API table;
# modules/modx_rt.c supplies memcpy/memset for compiler-generated calls. Without the ARM toolchain
# a descriptor-only placeholder stands in (the simulators run the linked host code anyway).
modx_obj = $(patsubst %.c,$(MODX_OBJDIR)/%.o,$(1))
MODX_CFLAGS = -std=c11 -Os $(ARCH) $(WARN) $(INCLUDES) $(DEFS) -DPLATFORM_STM32=1 -DMODX_IMAGE=1 \
	-fPIE -fvisibility=hidden -ffreestanding -fno-common -ffunction-sections -fdata-sections \
	-fno-tree-loop-distribute-patterns -fno-unwind-tables -fno-asynchronous-unwind-tables -Wdouble-promotion

$(MODX_OBJDIR)/%.o: %.c $(GEN_KEYMAP) Makefile $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(MODX_CFLAGS) -MMD -MP -c $< -o $@

ifneq ($(wildcard $(ARM_CC)),)
$(ANIM_ELF): $(call modx_obj,$(ANIM_SRC)) modules/modx_image.ld
	$(ARM_CC) $(ARCH) -nostdlib -nostartfiles -Wl,-T,modules/modx_image.ld -Wl,-q -Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments -Wl,-Map=$(MODX_OBJDIR)/animation.map -o $@ $(call modx_obj,$(ANIM_SRC))
$(ANIM_BIN): $(ANIM_ELF) tools/modlink.py
	$(ARM_OBJCOPY) -O binary -j .text $< $@
	$(PYTHON) tools/modlink.py --readelf $(ARM_PREFIX)readelf --elf $< --bin $@ --name animation --abi $(MODX_ABI)
else
$(ANIM_BIN): tools/mkmodule.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkmodule.py placeholder --name animation --abi $(MODX_ABI) --out $@
endif

ifneq ($(wildcard $(ARM_CC)),)
$(MQ_ELF): $(call modx_obj,$(MQ_SRC)) modules/modx_image.ld
	$(ARM_CC) $(ARCH) -nostdlib -nostartfiles -Wl,-T,modules/modx_image.ld -Wl,-q -Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments -Wl,-Map=$(MODX_OBJDIR)/mathquest.map -o $@ $(call modx_obj,$(MQ_SRC))
$(MQ_BIN): $(MQ_ELF) tools/modlink.py
	$(ARM_OBJCOPY) -O binary -j .text $< $@
	$(PYTHON) tools/modlink.py --readelf $(ARM_PREFIX)readelf --elf $< --bin $@ --name mathquest --abi $(MODX_ABI)
else
$(MQ_BIN): tools/mkmodule.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkmodule.py placeholder --name mathquest --abi $(MODX_ABI) --out $@
endif

# the advanced CAS extension: the CAS sources with the CAS warning set and its headers; libgcc for
# the 64-bit divisions (its routines are position-independent leaf code)
$(call modx_obj,$(CAS_EXT_SRC)): WARN = $(CAS_WARN) -I$(CAS_DIR)/src -I$(CAS_DIR)/ext
ifneq ($(wildcard $(ARM_CC)),)
$(CASX_ELF): $(call modx_obj,$(CASX_SRC)) modules/modx_image.ld
	$(ARM_CC) $(ARCH) -nostdlib -nostartfiles -Wl,-T,modules/modx_image.ld -Wl,-q -Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments -Wl,-Map=$(MODX_OBJDIR)/casx.map -o $@ $(call modx_obj,$(CASX_SRC)) -lgcc
$(CASX_BIN): $(CASX_ELF) tools/modlink.py
	$(ARM_OBJCOPY) -O binary -j .text $< $@
	$(PYTHON) tools/modlink.py --readelf $(ARM_PREFIX)readelf --elf $< --bin $@ --name casx --abi $(MODX_ABI)
else
$(CASX_BIN): tools/mkmodule.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkmodule.py placeholder --name casx --abi $(MODX_ABI) --out $@
endif

# the Geometry app: libgcc for what the compiler may call by itself (its routines are position-independent)
ifneq ($(wildcard $(ARM_CC)),)
$(GEO_ELF): $(call modx_obj,$(GEO_SRC)) modules/modx_image.ld
	$(ARM_CC) $(ARCH) -nostdlib -nostartfiles -Wl,-T,modules/modx_image.ld -Wl,-q -Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments -Wl,-Map=$(MODX_OBJDIR)/geometry.map -o $@ $(call modx_obj,$(GEO_SRC)) -lgcc
$(GEO_BIN): $(GEO_ELF) tools/modlink.py
	$(ARM_OBJCOPY) -O binary -j .text $< $@
	$(PYTHON) tools/modlink.py --readelf $(ARM_PREFIX)readelf --elf $< --bin $@ --name geometry --abi $(MODX_ABI)
else
$(GEO_BIN): tools/mkmodule.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkmodule.py placeholder --name geometry --abi $(MODX_ABI) --out $@
endif

# Python: a code-load image (not position-independent: linked at 0 with its static data, relocated by
# the loader), MicroPython at -Os with its own warnings, newlib's libm / libc for the maths and string
# functions. mp_iternext is wrapped so that loops inside built-ins (sum(range(10**8))) still poll
# the break key and the watchdog.
PY_IMG_CFLAGS = -Os $(ARCH) $(INCLUDES) $(DEFS) $(MPY_WARN) -DPLATFORM_STM32=1 -DMODX_IMAGE=1 -fno-common \
	-ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
$(call modx_obj,$(PY_SRC)): $(MODX_OBJDIR)/%.o: %.c $(AUTOCONF) | $(MPY_STAMP) $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(PY_IMG_CFLAGS) -MMD -MP -c $< -o $@
ifneq ($(wildcard $(ARM_CC)),)
$(PY_ELF): $(call modx_obj,$(PY_SRC)) modules/modx_load.ld
	$(ARM_CC) $(ARCH) -nostartfiles --specs=nano.specs -Wl,-T,modules/modx_load.ld -Wl,-q -Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments -Wl,--wrap=mp_iternext -Wl,--wrap=mp_iternext_allow_raise \
		-Wl,-Map=$(MODX_OBJDIR)/python.map -o $@ $(call modx_obj,$(PY_SRC)) -lm -lc -lgcc
$(PY_BIN): $(PY_ELF) tools/modreloc.py
	$(PYTHON) tools/modreloc.py --elf $< --out $@ --name python --abi $(MODX_ABI)
else
$(PY_BIN): tools/mkmodule.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkmodule.py placeholder --load --name python --abi $(MODX_ABI) --out $@
endif
ifneq ($(PY_MOD),)
$(PY_MOD): $(PY_BIN) tools/mkmodule.py
	$(PYTHON) tools/mkmodule.py pack --payload $(PY_BIN) --name python --version $(PY_VERSION) \
		--iface modx --abi $(MODX_ABI) --kind code-load --tier standard --kconfig MOD_PYTHON \
		--built-with "$(MOD_BUILT_WITH)" --out $@
endif

.PHONY: modx mathquest-module
modx: $(ANIM_BIN) $(MQ_BIN) $(CASX_BIN) $(GEO_BIN) $(PY_BIN)
# Math Quest is not in `make release`: this builds its module file for the microSD card
mathquest-module: $(MQ_MOD)
	@$(PYTHON) tools/mkmodule.py check $(MQ_MOD)

# ------------------------------------------------------------------ configuration targets
.PHONY: menuconfig defconfig savedefconfig
menuconfig:
	$(PYTHON) tools/kconfig.py menuconfig --config $(BUILD)/.config
defconfig:
	@echo "$(BUILD)/.config: $(KCONFIG_DEFCONFIG) (already applied by tools/kconfig.py sync)"
savedefconfig:
	$(PYTHON) tools/kconfig.py savedefconfig --config $(BUILD)/.config --out $(BUILD)/defconfig

# ------------------------------------------------------------------ firmware
FW_OBJDIR := $(BUILD)/fw
fw_obj = $(patsubst %.c,$(FW_OBJDIR)/%.o,$(1))
LDSCRIPT := platform/stm32h743/stm32h743zg.ld
ARCH := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
FW_CFLAGS = -std=c11 -O2 -g3 $(ARCH) $(CWARN) $(INCLUDES) $(DEFS) \
	-Iplatform/stm32h743 -Ithird_party/cmsis_core/Include -Ithird_party/cmsis_device_h7/Include \
	-DSTM32H743xx -DPLATFORM_STM32=1 -ffunction-sections -fdata-sections -fno-common \
	-fstack-usage $(FW_SSP) $(FW_WARN_EXTRA) $(FW_EXTRA)
FW_WARN_EXTRA = -Wdouble-promotion
# Stack protector (__stack_chk_fail in syscalls.c traps, the crash record names the
# function whose frame was overrun). Not in the CAS: its hot recursion stays as fast as before.
FW_SSP = -fstack-protector-strong
# Extra firmware defines, e.g. FW_EXTRA=-DSDRAM_SDCLK_MHZ=90 (use a separate BUILD= directory)
FW_EXTRA ?=
FW_LDFLAGS := $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD)/fw/calc.map \
	-Wl,--print-memory-usage -Wl,--no-warn-rwx-segments \
	--specs=nano.specs --specs=nosys.specs -u _printf_float
# USB device stack (TinyUSB, third_party/README.md): firmware only, with CONFIG_USB; the
# simulators model the device instead (platform/hostdev/usbhost.c)
TUSB_DIR := third_party/tinyusb/src
# usb.c counts the driver's events (diagnostics) by wrapping dcd_event_handler at the link
USB_WRAP := -Wl,--wrap=dcd_event_handler
TUSB_SRC := $(addprefix $(TUSB_DIR)/,tusb.c common/tusb_fifo.c device/usbd.c device/usbd_control.c \
	class/cdc/cdc_device.c class/msc/msc_device.c portable/synopsys/dwc2/dcd_dwc2.c portable/synopsys/dwc2/dwc2_common.c)
FW_ALL_SRC := $(CORE_SRC) $(APP_SRC) $(CAS_SRC) $(if $(CONFIG_CONTENT_INTERNAL),$(CAS_HOST_EXT)) $(FW_SRC) \
	$(if $(CONFIG_GEOMETRY_BUILTIN),$(GEO_MOD_SRC)) $(if $(CONFIG_USB),$(TUSB_SRC))
FW_OBJ := $(call fw_obj,$(FW_ALL_SRC))

# Objects depend on the Makefile so flag changes (e.g. -Os lists) rebuild them.
$(FW_OBJDIR)/%.o: %.c $(GEN_KEYMAP) Makefile $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FW_CFLAGS) -MMD -MP -c $< -o $@

$(call fw_obj,$(filter-out $(STUB_SRC),$(CAS_SRC))): CWARN = $(CAS_WARN)
$(call fw_obj,$(filter-out $(STUB_SRC),$(CAS_SRC))): FW_WARN_EXTRA =
$(call fw_obj,$(CAS_SRC) $(CAS_EXT_SRC)): FW_SSP =
$(call fw_obj,$(CAS_EXT_SRC)): CWARN = $(CAS_WARN) -I$(CAS_DIR)/src -I$(CAS_DIR)/ext
$(call fw_obj,$(CAS_EXT_SRC)): FW_WARN_EXTRA =

# App UI code is compiled for size; the per-pixel and per-sample loops (graph
# plotting and numerics, 3D mesh/raster/projection, stat drawing) stay at -O2.
FW_HOT_SRC := app/g3d_mesh.c app/g3d_raster.c app/g3d_view.c app/gr_plot.c app/gr_num.c \
	app/stat_draw.c app/m2d_box.c
$(call fw_obj,$(filter-out $(FW_HOT_SRC),$(filter app/%,$(APP_SRC)))): FW_CFLAGS += -Os
$(call fw_obj,$(GEO_MOD_SRC)): FW_CFLAGS += -Os -Imodules/geometry
# TinyUSB: its own warning set (vendored, unmodified), size-optimised; usb.c includes it
$(call fw_obj,$(TUSB_SRC)): CWARN = -Wall -Wno-unused-parameter
$(call fw_obj,$(TUSB_SRC)): FW_WARN_EXTRA =
$(call fw_obj,$(TUSB_SRC) platform/stm32h743/usb.c): FW_CFLAGS += -Os -I$(TUSB_DIR) -Iplatform/stm32h743
$(call fw_obj,core/vdrive.c core/usbdisk.c): FW_CFLAGS += -Os
# Cold code is compiled for size too (flash budget): console commands and diagnostics, the module
# store and the session log, the flash, power and sensor drivers (none of it is in a hot loop)
FW_COLD_SRC := core/console.c core/evlog.c core/modstore.c core/pstore.c core/safemode.c core/memtest.c core/patterns.c \
	platform/stm32h743/cmds.c platform/stm32h743/qspi.c platform/stm32h743/power.c platform/stm32h743/i2c.c \
	platform/stm32h743/adc.c
$(call fw_obj,$(FW_COLD_SRC)): FW_CFLAGS += -Os

$(FW_OBJDIR)/version.o: $(FW_OBJ) $(VERSION_STAMP) tools/gen_version.py
	$(call gen_version,$(ARM_CC) $(FW_CFLAGS))

$(BUILD)/fw/calc.elf: $(FW_OBJ) $(FW_OBJDIR)/version.o $(CAS_LIBS_FW) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FW_LDFLAGS) $(if $(CONFIG_USB),$(USB_WRAP)) -o $@ $(FW_OBJ) $(FW_OBJDIR)/version.o $(CAS_LIBS_FW) -lm

# Internal flash: the two 512 KB banks are not contiguous on the 1 MB part, so
# there are two raw images: calc.bin = bank 1 (0x08000000, vectors + code),
# calc_bank2.bin = bank 2 (0x08100000, section .flash2: CAS library, help
# manual). The .hex and .elf carry both. The optional fixed-address .qspi
# section (unused) goes to calc_xip.bin; the QSPI content is the module store
# build/modules/calc_qspi.bin (make modules).
$(BUILD)/fw/calc.bin: $(BUILD)/fw/calc.elf
	$(ARM_OBJCOPY) -O binary -R .flash2 -R .qspi -R .sdram -R .sdram_fb $< $@
$(BUILD)/fw/calc_bank2.bin: $(BUILD)/fw/calc.elf
	$(ARM_OBJCOPY) -O binary -j .flash2 $< $@
$(BUILD)/fw/calc.hex: $(BUILD)/fw/calc.elf
	$(ARM_OBJCOPY) -O ihex -R .qspi -R .sdram -R .sdram_fb $< $@
$(BUILD)/fw/calc_xip.bin: $(BUILD)/fw/calc.elf
	$(ARM_OBJCOPY) -O binary -j .qspi $< $@

FLASH_BUDGET_ARGS = --budget $(CONFIG_FLASH_BUDGET_PCT) --store $(STORE_BIN)
.PHONY: fw
fw: $(BUILD)/fw/calc.bin $(BUILD)/fw/calc_bank2.bin $(BUILD)/fw/calc.hex $(BUILD)/fw/calc_xip.bin modules
	@echo "---- size (CAS=$(CAS), tier $(if $(CONFIG_CONTENT_INTERNAL),core,$(if $(CONFIG_CAS_EXT),advanced,standard))) ----"
	@$(ARM_SIZE) -B $(BUILD)/fw/calc.elf
	@$(ARM_SIZE) -A $(BUILD)/fw/calc.elf | awk 'NR<=2 || $$2>0' | tee $(BUILD)/fw/size.txt
	@$(PYTHON) tools/flash_budget.py $(BUILD)/fw/calc.elf $(FLASH_BUDGET_ARGS)

# Flash bytes per module and per group, split by bank (from the linker map)
.PHONY: flash-report
flash-report: fw
	@$(PYTHON) tools/flash_budget.py $(BUILD)/fw/calc.elf --modules $(BUILD)/fw/calc.map --top 40 $(FLASH_BUDGET_ARGS)

# ------------------------------------------------------------------ bring-up image
# Minimal first-flash firmware (platform/stm32h743/bringup): clocks, LED, console,
# rail/SDRAM/LCD/touch tests; no app, no CAS, 3V3_BUS off until asked.
BU_OBJDIR := $(BUILD)/bringup
bu_obj = $(patsubst %.c,$(BU_OBJDIR)/%.o,$(1))
BU_CORE := core/console.c core/evlog.c core/safemode.c core/gfx.c core/font.c core/keyq.c core/patterns.c core/rle.c core/memtest.c \
	core/touch.c core/gen/keymap_gen.c core/gen/fonts_gen.c
BU_SRC := $(BU_CORE) $(GEN_CONFIG) $(filter-out platform/stm32h743/main.c,$(FW_SRC)) platform/stm32h743/bringup/bringup_main.c
BU_OBJ := $(call bu_obj,$(BU_SRC))

$(BU_OBJDIR)/%.o: %.c $(GEN_KEYMAP) $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FW_CFLAGS) -DBRINGUP=1 -Os -MMD -MP -c $< -o $@

$(BU_OBJDIR)/version.o: $(BU_OBJ) $(VERSION_STAMP) tools/gen_version.py
	$(call gen_version,$(ARM_CC) $(FW_CFLAGS) -Os)

$(BU_OBJDIR)/bringup.elf: $(BU_OBJ) $(BU_OBJDIR)/version.o $(LDSCRIPT)
	$(ARM_CC) $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BU_OBJDIR)/bringup.map -Wl,--no-warn-rwx-segments \
		--specs=nano.specs --specs=nosys.specs -u _printf_float -o $@ $(BU_OBJ) $(BU_OBJDIR)/version.o -lm
$(BU_OBJDIR)/bringup.bin: $(BU_OBJDIR)/bringup.elf
	$(ARM_OBJCOPY) -O binary -R .qspi -R .sdram -R .sdram_fb $< $@
$(BU_OBJDIR)/bringup.hex: $(BU_OBJDIR)/bringup.elf
	$(ARM_OBJCOPY) -O ihex -R .qspi -R .sdram -R .sdram_fb $< $@

.PHONY: bringup
bringup: $(BU_OBJDIR)/bringup.bin $(BU_OBJDIR)/bringup.hex
	@$(ARM_SIZE) -B $(BU_OBJDIR)/bringup.elf

# ------------------------------------------------------------------ release
# Flashable artifacts + checksums in release/ (see release/FLASHING.md): the firmware of this
# configuration (normally the standard tier), its content modules (MODULES/*.MOD + INDEX.TXT for
# the microSD card, calc_qspi.bin for a probe), and the core-tier rollback
# firmware calc_core*.{bin,hex} that needs no QSPI content at all. Module files are named by
# version (PYTHO021.MOD = python 0.2.1): release/MODULES holds this release's files only (those of
# earlier releases are removed; a file of the same version and payload is kept as it is) and
# INDEX.TXT lists them.
#
# Versions (README.md "Versions and releases"): one version per release. `make release` builds at
# the version of versions.txt, then tools/release_version.py raises its PATCH when the firmware's
# images (the version strings masked out) or any module's payload differ from the last release's
# (release/MANIFEST.txt "version" lines); the build then runs again with the new version (the
# version objects and every module file, all stamped with it) and the images are copied. Nothing
# changed: no bump. After committing the release (release/, versions.txt): `make release-tag` tags
# it vX.Y.Z. MAJOR / MINOR by hand: `make version-bump PART=major|minor|patch`.
RELEASE := release
# the images whose content decides a firmware bump (the version strings in them masked out)
RELEASE_FW_BINS = $(BUILD)/fw/calc.bin $(BUILD)/fw/calc_bank2.bin $(BUILD)/tier-core/fw/calc.bin \
	$(BUILD)/tier-core/fw/calc_bank2.bin $(BU_OBJDIR)/bringup.bin
.PHONY: release release-build release-images release-tag version-bump
release:
	$(MAKE) --no-print-directory FW_RELEASE=1 release-build
	$(PYTHON) tools/release_version.py bump --release $(RELEASE) --fw-bin $(RELEASE_FW_BINS) \
		--modules $(MODULE_FILES) $(MQ_MOD)
	$(MAKE) --no-print-directory FW_RELEASE=1 release-images

release-build: fw bringup
	$(MAKE) --no-print-directory BUILD=$(BUILD)/tier-core TIER=core CAS= CONTENT= MANUAL= FW_RELEASE=$(FW_RELEASE) fw

release-images: release-build
	@mkdir -p $(RELEASE)/MODULES
	cp $(BUILD)/fw/calc.elf $(BUILD)/fw/calc.bin $(BUILD)/fw/calc_bank2.bin $(BUILD)/fw/calc.hex \
		$(BUILD)/fw/calc.map $(RELEASE)/
	cp $(BU_OBJDIR)/bringup.elf $(BU_OBJDIR)/bringup.bin $(BU_OBJDIR)/bringup.hex $(RELEASE)/
	rm -f $(RELEASE)/MODULES/MANUAL.MOD $(RELEASE)/calc_qspi.bin
	cp $(NB_HTML) $(RELEASE)/CALCULATOR.HTML
	@if [ -n "$(MODULE_FILES)" ]; then $(PYTHON) tools/release_version.py library $(RELEASE)/MODULES $(MODULE_FILES) && \
		$(PYTHON) tools/mkmodule.py index --out $(RELEASE)/MODULES/INDEX.TXT $(RELEASE)/MODULES/*.MOD && \
		cat $(RELEASE)/MODULES/INDEX.TXT && \
		cp $(STORE_BIN) $(RELEASE)/; else echo "(no content modules in this configuration)"; fi
	cp $(BUILD)/tier-core/fw/calc.elf $(RELEASE)/calc_core.elf
	cp $(BUILD)/tier-core/fw/calc.bin $(RELEASE)/calc_core.bin
	cp $(BUILD)/tier-core/fw/calc_bank2.bin $(RELEASE)/calc_core_bank2.bin
	cp $(BUILD)/tier-core/fw/calc.hex $(RELEASE)/calc_core.hex
	$(PYTHON) tools/release_info.py $(RELEASE) $(CAS) > $(RELEASE)/MANIFEST.txt || \
		{ cat $(RELEASE)/MANIFEST.txt; exit 1; }
	$(PYTHON) tools/release_version.py manifest --fw-bin $(RELEASE_FW_BINS) --modules $(MODULE_FILES) $(MQ_MOD) \
		>> $(RELEASE)/MANIFEST.txt
	@if [ -n "$(MODULE_FILES)" ]; then $(PYTHON) tools/mkmodule.py check $(RELEASE)/MODULES/*.MOD \
		>> $(RELEASE)/MANIFEST.txt; fi
	cd $(RELEASE) && shasum -a 256 *.elf *.bin *.hex CALCULATOR.HTML $$(ls MODULES/* 2>/dev/null) > SHA256SUMS
	@cat $(RELEASE)/MANIFEST.txt; cat $(RELEASE)/SHA256SUMS
	@echo "release $(FW_VERSION): commit release/ and versions.txt, then make release-tag"

release-tag:
	$(PYTHON) tools/release_version.py tag --release $(RELEASE)

PART ?= patch
version-bump:
	$(PYTHON) tools/release_version.py set $(PART)

# ------------------------------------------------------------------ web (WebAssembly)
# Emscripten from a local emsdk checkout (no sudo):
#   git clone https://github.com/emscripten-core/emsdk.git ../tools/emsdk
#   cd ../tools/emsdk && ./emsdk install $(EMSDK_VERSION) && ./emsdk activate $(EMSDK_VERSION)
# (emsdk needs python >= 3.10 to install: EMSDK_PYTHON=/path/to/python3.10+ ./emsdk install ...)
EMSDK         ?= ../tools/emsdk
EMSDK_VERSION := 6.0.12
EMSDK_PY      := $(firstword $(wildcard $(EMSDK)/python/*/bin/python3))
EMCC          ?= $(if $(EMSDK_PY),EMSDK_PYTHON=$(EMSDK_PY) )$(EMSDK)/upstream/emscripten/emcc
EMSDK_NODE    ?= $(firstword $(wildcard $(EMSDK)/node/*/bin/node))
NODE          ?= $(if $(EMSDK_NODE),$(EMSDK_NODE),node)
WEB_PYTHON    ?= ../.venv/bin/python
WEB_OBJDIR := $(BUILD)/web
web_obj = $(patsubst %.c,$(WEB_OBJDIR)/%.o,$(1))
# The web pages' factory content leaves the browser notebook out (it is a page itself: no drive serves it there).
WEB_CONTENT_C := $(BUILD)/gen/content_img_web.c
WEB_SRC := $(CORE_SRC) $(APP_SRC) $(MODX_HOST_SRC) $(CAS_SRC) $(CAS_HOST_EXT) $(sort $(wildcard platform/web/*.c)) \
	$(filter-out $(CONTENT_C),$(HOSTDEV_SRC)) $(WEB_CONTENT_C)
WEB_MODULE_FILES = $(filter-out $(WEBUI_MOD),$(MODULE_FILES))
$(WEB_CONTENT_C): $(MODULE_FILES) $(MQ_MOD) tools/mkmodule.py $(AUTOCONF) $(VERSIONS)
	$(PYTHON) tools/mkmodule.py store --out $(BUILD)/gen/calc_qspi_web.bin --c-out $@ $(filter-out $(WEBUI_MOD),$(STORE_MODS)) \
		$(if $(WEB_MODULE_FILES),--library $(WEB_MODULE_FILES)) $(if $(MQ_MOD),--extra $(MQ_MOD))
WEB_CFLAGS = -std=c11 -O2 $(CWARN) $(INCLUDES) $(DEFS) -Iplatform/web -Iplatform/hostdev -DPLATFORM_WEB=1
WEB_LDFLAGS := -O2 --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createCalcModule -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0 \
	-sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=48MB -sFILESYSTEM=0 \
	-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPU16 \
	-sEXPORTED_FUNCTIONS=_malloc,_free -sSTACK_SIZE=1MB
WEB_HTML   ?= web/dist/calculator.html
WEB_ASSETS := web/assets/calc_1x.webp web/assets/calc_2x.webp web/assets/keymap.json

$(WEB_OBJDIR)/%.o: %.c $(GEN_KEYMAP) $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CFLAGS) -MMD -MP -c $< -o $@

$(call web_obj,$(filter-out $(STUB_SRC),$(CAS_SRC))): CWARN = $(CAS_WARN)
$(call web_obj,$(CAS_EXT_SRC)): CWARN = $(CAS_WARN) -I$(CAS_DIR)/src -I$(CAS_DIR)/ext

$(WEB_OBJDIR)/version.o: $(call web_obj,$(WEB_SRC)) $(VERSION_STAMP) tools/gen_version.py
	$(call gen_version,$(EMCC) $(WEB_CFLAGS))

$(BUILD)/web/calc_module.js: $(call web_obj,$(WEB_SRC)) $(WEB_OBJDIR)/version.o
	@if [ "$(CAS)" = lib ]; then echo "CAS=lib is not supported for web (use stub or src)"; exit 1; fi
	$(EMCC) $(WEB_LDFLAGS) -o $@ $(call web_obj,$(WEB_SRC)) $(WEB_OBJDIR)/version.o -lm

$(WEB_HTML): $(BUILD)/web/calc_module.js web/page/calculator.html.in web/page/calc_page.js \
		web/page/calc_api.js web/tools/build_html.py $(WEB_ASSETS)
	$(PYTHON) web/tools/build_html.py --module $(BUILD)/web/calc_module.js --out $@

.PHONY: web web-test web-image
web: $(WEB_HTML)
	@ls -l $< | awk '{print "web page: " $$NF " (" $$5 " bytes, CAS=$(CAS))"}'

web-test: web $(BUILD)/sim/calcsim
	$(NODE) web/test/run_web_test.js --module $(BUILD)/web/calc_module.js --sim $(BUILD)/sim/calcsim \
		--out web/test --page $(WEB_HTML)
	@if [ -x "$(WEB_PYTHON)" ]; then $(WEB_PYTHON) web/tools/composite.py web/test/seq_yeq_sin_x_graph.png \
		web/test/composite_graph.png; else echo "($(WEB_PYTHON) not found: composite image skipped)"; fi

web-image:
	$(WEB_PYTHON) web/tools/render_calc.py --out web/assets

# ---- the standalone Sparkle Town page: the Math Quest module alone, full window, high resolution
# (platform/web/web_sparkle.c), sound (web/page/sparkle_audio.js). Its own objects (-DWEB_SPARKLE).
SPARKLE_OBJDIR := $(BUILD)/web-sparkle
sparkle_obj = $(patsubst %.c,$(SPARKLE_OBJDIR)/%.o,$(1))
SPARKLE_HTML ?= web/dist/sparkle.html
SPARKLE_ART := web/assets/sparkle_art.webp web/assets/sparkle_art.json
SPARKLE_PAGE := web/page/sparkle.html.in web/page/sparkle_page.js web/page/sparkle_vector.js web/page/sparkle_rigs.js web/page/sparkle_audio.js \
                web/page/sparkle_voice.js \
                web/page/calc_api.js
SPARKLE_VECTOR := $(wildcard modules/mathquest/art_vector/index.json modules/mathquest/art_vector/*/*.svg)

$(SPARKLE_OBJDIR)/%.o: %.c $(GEN_KEYMAP) $(AUTOCONF) | $(GEN_FONTS)
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CFLAGS) -DWEB_SPARKLE=1 -MMD -MP -c $< -o $@
$(call sparkle_obj,$(filter-out $(STUB_SRC),$(CAS_SRC))): CWARN = $(CAS_WARN)
$(call sparkle_obj,$(CAS_EXT_SRC)): CWARN = $(CAS_WARN) -I$(CAS_DIR)/src -I$(CAS_DIR)/ext

$(SPARKLE_OBJDIR)/version.o: $(call sparkle_obj,$(WEB_SRC)) $(VERSION_STAMP) tools/gen_version.py
	$(call gen_version,$(EMCC) $(WEB_CFLAGS) -DWEB_SPARKLE=1)

$(SPARKLE_OBJDIR)/sparkle_module.js: $(call sparkle_obj,$(WEB_SRC)) $(SPARKLE_OBJDIR)/version.o
	@if [ "$(CAS)" = lib ]; then echo "CAS=lib is not supported for web (use stub or src)"; exit 1; fi
	$(EMCC) $(WEB_LDFLAGS) -o $@ $(call sparkle_obj,$(WEB_SRC)) $(SPARKLE_OBJDIR)/version.o -lm

$(SPARKLE_HTML): $(SPARKLE_OBJDIR)/sparkle_module.js $(SPARKLE_PAGE) $(SPARKLE_ART) $(SPARKLE_VECTOR) web/tools/build_sparkle_html.py
	$(PYTHON) web/tools/build_sparkle_html.py --module $(SPARKLE_OBJDIR)/sparkle_module.js --out $@

.PHONY: sparkle-web sparkle-test sparkle-art sparkle-vector
sparkle-web: $(SPARKLE_HTML)
	@ls -l $< | awk '{print "sparkle page: " $$NF " (" $$5 " bytes)"}'
sparkle-test: sparkle-web
	$(NODE) web/test/run_sparkle_test.js --page $(SPARKLE_HTML)
sparkle-art:
	$(WEB_PYTHON) tools/gen_mq_art_hires.py --out web/assets/sparkle_art

# ---- the Sparkle Town playtest bot (tests/bot/README.md): strategies + invariants on the simulator
# and on the Mac page in headless Chrome. The page is a harness build (build/bot/sparkle.html, with
# the module's read-only `mq hits` debug hook); without emsdk the bot falls back to web/dist.
BOT_PAGE := $(BUILD)/bot/sparkle.html
# Headless test browsers left behind (tools/headless_chrome.js normally kills them on any exit): only the ones a
# harness launched -- headless, with a calc-headless-* (or an older sparkle-bot-/ra-/shots-) profile -- never the
# user's own Chrome; each with its helper processes, and their profiles and browser slots removed.
.PHONY: kill-test-browsers
kill-test-browsers:
	@pids=$$(pgrep -f -- '--headless.*--user-data-dir=[^ ]*(calc-headless-|sparkle-bot-|sparkle-ra-|sparkle-shots-)' || true); \
	for p in $$pids; do pkill -KILL -P $$p 2>/dev/null; kill -KILL -- -$$p 2>/dev/null; kill -KILL $$p 2>/dev/null; done; \
	t=$${TMPDIR:-/tmp}; rm -rf "$$t"/calc-headless-* "$$t"/sparkle-bot-* "$$t"/sparkle-ra-* "$$t"/sparkle-shots-* 2>/dev/null; \
	echo "test browsers killed: $$(echo $$pids | wc -w | tr -d ' ')"

.PHONY: bot bot-long bot-page
bot-page:
	-$(MAKE) sparkle-web SPARKLE_HTML=$(BOT_PAGE)
bot: $(BUILD)/sim/calcsim modx bot-page
	$(PYTHON) tests/bot/bot.py quick
bot-long: $(BUILD)/sim/calcsim modx bot-page
	$(PYTHON) tests/bot/bot.py long --save-regressions $(if $(SPARKLE_PRIVATE_PAGE),--theme-page $(SPARKLE_PRIVATE_PAGE))
sparkle-vector:
	$(PYTHON) tools/gen_mq_vector.py --out modules/mathquest/art_vector

# Render audit (docs/audit/RENDER_AUDIT.md): every scene drawn three ways from one module state --
# the 1x framebuffer, the Mac page as it is, the page with no art -- in headless Google Chrome, and
# compared (presence, tint, swallowed content, text, occlusion with timelines, animation); then the
# same scenes in the native simulator against the page's 1x. Results, side-by-side images and the
# grouped findings go to RENDER_AUDIT_OUT. A private build with a theme pack is audited too when
# RENDER_AUDIT_PRIVATE names its page (its images stay where RENDER_AUDIT_PRIVATE_OUT says, never here).
#   make render-audit [RENDER_AUDIT_JOBS=ui,games,...] [RENDER_AUDIT_ONLY=substr] [RENDER_AUDIT_PRIVATE=page.html]
RENDER_AUDIT_OUT ?= $(BUILD)/render_audit
RENDER_AUDIT_JOBS ?= ui,games,mechs,twists,town,events,screens,occl,overlays
RENDER_AUDIT_NODE ?= node
RENDER_AUDIT_PRIVATE ?=
RENDER_AUDIT_PRIVATE_OUT ?= $(RENDER_AUDIT_OUT)
RA_TOOLS := docs/audit/render/tools
.PHONY: render-audit
render-audit: $(BUILD)/sim/calcsim
	@test -f $(SPARKLE_HTML) || $(MAKE) sparkle-web
	$(RENDER_AUDIT_NODE) $(RA_TOOLS)/render_audit.js --page $(SPARKLE_HTML) --label public --out $(RENDER_AUDIT_OUT) \
		--jobs $(RENDER_AUDIT_JOBS) $(if $(RENDER_AUDIT_ONLY),--only $(RENDER_AUDIT_ONLY)) --simscripts $(RENDER_AUDIT_OUT)/simscripts
	$(PYTHON) $(RA_TOOLS)/sim_compare.py $(RENDER_AUDIT_OUT)/simscripts $(RENDER_AUDIT_OUT)/public/fb --sim $(BUILD)/sim/calcsim --out $(RENDER_AUDIT_OUT)/sim
	$(if $(RENDER_AUDIT_PRIVATE),$(RENDER_AUDIT_NODE) $(RA_TOOLS)/render_audit.js --page $(RENDER_AUDIT_PRIVATE) --label private \
		--out $(RENDER_AUDIT_PRIVATE_OUT) --jobs $(RENDER_AUDIT_JOBS) $(if $(RENDER_AUDIT_ONLY),--only $(RENDER_AUDIT_ONLY)))
	$(PYTHON) $(RA_TOOLS)/aggregate.py $(RENDER_AUDIT_OUT)/public/results.jsonl \
		$(if $(RENDER_AUDIT_PRIVATE),$(RENDER_AUDIT_PRIVATE_OUT)/private/results.jsonl) --json $(RENDER_AUDIT_OUT)/groups.json \
		> $(RENDER_AUDIT_OUT)/groups.txt
	@head -2 $(RENDER_AUDIT_OUT)/groups.txt; echo "render audit: $(RENDER_AUDIT_OUT)/groups.txt (images: $(RENDER_AUDIT_OUT)/public/shots)"

# ------------------------------------------------------------------ generated
# (GNU make 3.81 on macOS has no grouped targets: generate both from one rule.)
core/gen/keymap_gen.h: tools/gen_keymap.py keys/layout.py app/keys.c ../hardware/gen/design.py ../hardware/pinmap/pinmap.py
	$(PYTHON) tools/gen_keymap.py core/gen
core/gen/keymap_gen.c: core/gen/keymap_gen.h
	@test -f $@ || $(PYTHON) tools/gen_keymap.py core/gen

core/gen/fonts_gen.c: tools/gen_font.py $(wildcard third_party/fonts/*.bdf)
	$(PYTHON) tools/gen_font.py $@

.PHONY: gen check-pins all clean
gen: $(GEN_KEYMAP) $(GEN_FONTS)

check-pins:
	$(PYTHON) tools/check_pins.py

all: sim fw bringup test

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
