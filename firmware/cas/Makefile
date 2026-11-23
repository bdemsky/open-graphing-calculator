# CAS engine build: host library, REPL, tests, and ARM cross-compile check.
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter
# The extensions (ext/*/: casx.h) are linked into every host build and registered at the
# first extension lookup (CASX_BUILTIN), so all tests see the full command set.
EXT_SRCS:= $(wildcard ext/*/*.c)
SRCS    := $(wildcard src/*.c) $(EXT_SRCS)
HDRS    := src/cas_int.h src/poly.h src/upoly.h src/casx.h ext/casx_mod.h include/cas.h $(wildcard ext/*/*.h)
XFLAGS  := -DCASX_BUILTIN -Isrc -Iext
OBJS    := $(patsubst %.c,build/host/%.o,$(SRCS))
ARMGCC  ?= /Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin/arm-none-eabi-gcc
ARMFLAGS:= -std=c11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -O2 -ffreestanding \
           -Wall -Wextra -Wno-unused-parameter -fno-common
ARMOBJS := $(patsubst %.c,build/arm/%.o,$(SRCS))
PY      ?= ../../.venv/bin/python

.PHONY: all repl test unit difftest fuzz arm-check clean bench wester wester-asan wester-baselines stress \
        suites suites-fetch suites-translate

all: repl

build/host/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(XFLAGS) -c $< -o $@

build/libcas.a: $(OBJS)
	rm -f $@ && ar rcs $@ $^

build/cas_repl: tools/cas_repl.c build/libcas.a
	$(CC) $(CFLAGS) $< build/libcas.a -lm -lpthread -o $@

repl: build/cas_repl

build/unit_tests: tests/unit_tests.c $(wildcard tests/*.inc) build/libcas.a
	$(CC) $(CFLAGS) -Isrc $< build/libcas.a -lm -o $@

SANFLAGS := -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall
ASANOBJS := $(patsubst %.c,build/asan/%.o,$(SRCS))

build/asan/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(SANFLAGS) $(XFLAGS) -c $< -o $@

build/fuzz: tests/fuzz.c $(ASANOBJS)
	$(CC) $(SANFLAGS) -Isrc $< $(ASANOBJS) -lm -o $@

unit: build/unit_tests
	./build/unit_tests

fuzz: build/fuzz
	./build/fuzz 100000

difftest: build/cas_repl
	$(PY) tests/difftest.py

test: unit fuzz difftest

bench: build/cas_repl
	$(PY) tests/bench.py

# Wester benchmark (tests/wester): problems.txt graded with SymPy; writes
# tests/wester/results.{json,md}.  wester-asan reruns it on the sanitizer build.
build/cas_repl_asan: tools/cas_repl.c $(ASANOBJS)
	$(CC) $(SANFLAGS) $< $(ASANOBJS) -lm -lpthread -o $@

wester: build/cas_repl
	$(PY) tests/wester/run_wester.py

# Resource-exhaustion stress test (tests/stress/run_stress.py): Wester, a suite sample, the fuzz
# corpus and adversarial inputs on the sanitizer build with a 2 MB and a 256 KB arena and a 16 KB
# stack limit; every case must end in a result or a clean CAS error.
stress: build/cas_repl_asan build/fuzz
	$(PY) tests/stress/run_stress.py

wester-asan: build/cas_repl_asan
	$(PY) tests/wester/run_wester.py --asan --timeout 60000 -j 4

# Baselines: the same problems on Giac, SymPy and Maxima (whichever are
# installed; GIAC=/path/to/icas, MAXIMA=/path/to/maxima), graded by the same
# checker; writes tests/wester/baselines/<engine>/results.* and COMPARISON.md.
wester-baselines:
	$(PY) tests/wester/baselines/run_baselines.py

build/arm/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(ARMGCC) $(ARMFLAGS) $(XFLAGS) -c $< -o $@

build/libcas_arm.a: $(ARMOBJS)
	rm -f $@ && $(dir $(ARMGCC))arm-none-eabi-ar rcs $@ $^

arm-check: build/libcas_arm.a
	$(dir $(ARMGCC))arm-none-eabi-size -t $^ | tail -1

clean:
	rm -rf build

# Third-party test suites (tests/suites, see REPORT.md there): Rubi (Maxima
# syntax port, fetched with git), Maxima's rtest files (local Maxima install)
# SymPy's own tests (project venv) and Giac's check/ directory (source
# tarball, fetched by tests/suites/giac/fetch.sh; only check/ is extracted).  Translators and runners are memory
# guarded (2 GB per process, at most 3 jobs; lib/suitelib.py).
SUITES     ?= maxima,sympy,rubi,giac
SUITEB     := build/suites
RUBI_URL   := https://github.com/RuleBasedIntegration/MaximaSyntaxTestSuite
MAXIMA_TESTS ?= $(firstword $(wildcard /opt/local/share/maxima/*/tests /usr/local/share/maxima/*/tests /usr/share/maxima/*/tests))
SUITE_LIB  := $(wildcard tests/suites/lib/*.py)

suites-fetch:
	@mkdir -p $(SUITEB)/src
	@test -d $(SUITEB)/src/rubi || git clone -q --depth 1 $(RUBI_URL) $(SUITEB)/src/rubi
	@test -n "$(MAXIMA_TESTS)" || (echo "Maxima tests not found: set MAXIMA_TESTS=<maxima>/tests" && false)
	@sh tests/suites/giac/fetch.sh $(SUITEB)/src/giac

$(SUITEB)/rubi/cases.jsonl: tests/suites/rubi/translate.py $(SUITE_LIB) | suites-fetch
	@mkdir -p $(dir $@)
	$(PY) tests/suites/rubi/translate.py $(SUITEB)/src/rubi $@

$(SUITEB)/maxima/cases.jsonl: tests/suites/maxima/translate.py tests/suites/maxima/overrides.py $(SUITE_LIB) | suites-fetch
	@mkdir -p $(dir $@)
	$(PY) tests/suites/maxima/translate.py $(MAXIMA_TESTS) $@

$(SUITEB)/sympy/cases.jsonl: tests/suites/sympy/extract.py tests/suites/sympy/overrides.py $(SUITE_LIB)
	@mkdir -p $(dir $@)
	$(PY) tests/suites/sympy/extract.py $@

$(SUITEB)/giac/cases.jsonl: tests/suites/giac/translate.py $(SUITE_LIB) | suites-fetch
	@mkdir -p $(dir $@)
	$(PY) tests/suites/giac/translate.py $(firstword $(wildcard $(SUITEB)/src/giac/giac-*/check)) $@

suites-translate: $(SUITEB)/rubi/cases.jsonl $(SUITEB)/maxima/cases.jsonl $(SUITEB)/sympy/cases.jsonl \
                  $(SUITEB)/giac/cases.jsonl

suites: build/cas_repl suites-translate
	$(PY) tests/suites/run_suites.py --suite $(SUITES) -j 3

