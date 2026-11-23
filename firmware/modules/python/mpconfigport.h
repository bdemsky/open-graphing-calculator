/*
 * MicroPython configuration of the calculator's Python module (modules/python,
 * docs/QSPI_CONTENT.md 12.8). One configuration for the firmware image (ARM,
 * code-load module) and the simulators (linked into the host binary): the
 * generated qstr tables ($(BUILD)/gen/mpy/genhdr) are shared, so everything
 * that changes the set of names must be the same for both.
 *
 * Start from the "extra features" level and switch off what needs an
 * operating system (files, sockets, threads, select, machine) or would only
 * cost code (native emitters, frozen modules, .mpy loading).
 */
#ifndef PY_MPCONFIGPORT_H
#define PY_MPCONFIGPORT_H
#include <stdint.h>
#include <alloca.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

/* core */
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)          /* mp_repl_continue_with_input: multi-line blocks */
#define MICROPY_REPL_AUTO_INDENT (0)     /* the screen indents by itself */
#define MICROPY_REPL_EMACS_KEYS (0)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_PY_BUILTINS_COMPLEX (1)
#define MICROPY_PY_CMATH (1)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_MATH_SPECIAL_FUNCTIONS (1)
#define MICROPY_PY_MATH_FACTORIAL (1)
#define MICROPY_PY_MATH_ISCLOSE (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_STACK_CHECK_MARGIN (2048) /* bytes kept free below the limit for C calls between checks */
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_ENABLE_SCHEDULER (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (0)
#define MICROPY_PERSISTENT_CODE_LOAD (0)
#define MICROPY_MODULE_FROZEN_MPY (0)
#define MICROPY_MODULE_FROZEN_STR (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0) /* no file system in phase 1: import finds built-in modules only */
#define MICROPY_READER_VFS (0)
#define MICROPY_VFS (0)
#define MICROPY_PY_IO (1)              /* io.StringIO / BytesIO (json needs them); no files */
#define MICROPY_PY_OS (0)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_STDFILES (0)
#define MICROPY_PY_SYS_STDIO_BUFFER (0)
#define MICROPY_PY_SYS_PLATFORM "calculator"
#define MICROPY_PY_SYS_PS1_PS2 (0)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_PY_BUILTINS_EXECFILE (0)
#define MICROPY_PY_BUILTINS_HELP (1)
#define MICROPY_PY_BUILTINS_HELP_TEXT py_help_text
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)
#define MICROPY_PY_THREAD (0)
#define MICROPY_PY_SELECT (0)
#define MICROPY_PY_ASYNCIO (0)
#define MICROPY_PY_UCTYPES (0)
#define MICROPY_PY_DEFLATE (0)
#define MICROPY_PY_HASHLIB (0)
#define MICROPY_PY_BINASCII (0)
#define MICROPY_PY_FRAMEBUF (0)
#define MICROPY_PY_PLATFORM (0)
#define MICROPY_PY_MACHINE (0)
#define MICROPY_PY_JSON (1)
#define MICROPY_PY_RE (1)
#define MICROPY_PY_HEAPQ (1)
#define MICROPY_PY_RANDOM (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (py_random_seed())
#define MICROPY_PY_TIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS (0)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (0)
#define MICROPY_PY_COLLECTIONS_DEQUE (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (1)
#define MICROPY_PY_STRUCT (1)
#define MICROPY_PY_ARRAY (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_MICROPYTHON (1)
#define MICROPY_PY_BUILTINS_SET (1)
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS (1)
#define MICROPY_PY_BUILTINS_PROPERTY (1)
#define MICROPY_PY_FSTRINGS (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS (1)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_CPYTHON_COMPAT (1)
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_USE_INTERNAL_ERRNO (1)
#define MICROPY_ROM_TEXT_COMPRESSION (0)
#define MICROPY_EMIT_THUMB (0)
#define MICROPY_EMIT_INLINE_THUMB (0)
#define MICROPY_PY_SYS_SETTRACE (0)
#define MICROPY_ALLOC_PATH_MAX (128)
#define MICROPY_GC_ALLOC_THRESHOLD (1)

/* the NLR (exception) mechanism: setjmp in the simulators (the sanitizers know longjmp),
 * the Thumb-2 assembly on the calculator */
#if defined(MODX_IMAGE)
#define MICROPY_NLR_SETJMP (0)
#define MICROPY_GCREGS_SETJMP (0)
#else
#define MICROPY_NLR_SETJMP (1)
#define MICROPY_GCREGS_SETJMP (1)
#endif

/* Responsiveness: every 64 backward jumps / returns the VM calls py_poll()
 * (modules/python/py_entry.c): the ON key (KeyboardInterrupt), the
 * watchdog, USB and the screen's output stay serviced in long loops. */
void py_poll(void);
#define MICROPY_VM_HOOK_COUNT (64)
#define MICROPY_VM_HOOK_INIT static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL                       \
    if (--vm_hook_divisor == 0) {                  \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT;   \
        py_poll();                                 \
    }
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL

/* output: print() and tracebacks go to the module's capture (screen, console, notebook) */
void py_out(const char *s, size_t n);
#define MP_PLAT_PRINT_STRN(str, len) py_out(str, len)

uint32_t py_random_seed(void);
extern const char py_help_text[];

#define MP_SSIZE_MAX INTPTR_MAX /* (newlib's limits.h has no SSIZE_MAX without POSIX) */
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;

#define MICROPY_MPHALPORT_H "mphalport.h"
#define MICROPY_HW_BOARD_NAME "calculator"
#define MICROPY_HW_MCU_NAME "STM32H743"
#define MP_STATE_PORT MP_STATE_VM

#endif
