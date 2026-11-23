/* Minimal newlib system calls: stdout/stderr go to the UART console, heap is
 * bounded by the AXI SRAM region (see stm32h743zg.ld).
 * There is no file system behind these: fds 0-2 are the console and every
 * other call is a stub that reports a character device or fails. */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include "platform.h"

extern char end[];          /* start of heap (linker) */
extern char _heap_limit[];  /* end of AXI SRAM */

int _write(int fd, const char *buf, int len);
int _read(int fd, char *buf, int len);
void *_sbrk(ptrdiff_t incr);
int _close(int fd);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _lseek(int fd, int off, int whence);
int _kill(int pid, int sig);
int _getpid(void);
void _exit(int status);

/* Grow (or shrink) the heap for malloc. The heap runs from `end` (after .bss
 * and the fallback arena) up to _heap_limit; it never reaches the stack, which
 * lives in DTCM. Returns the old break, or (void *)-1 with errno = ENOMEM. */
void *_sbrk(ptrdiff_t incr)
{
    static char *brk = end;
    if (brk + incr > _heap_limit || brk + incr < end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *old = brk;
    brk += incr;
    return old;
}

/* stdout and stderr -> USART1 (uart_write busy-waits on the TX FIFO and
 * expands \n to \r\n). */
int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    uart_write(buf, (size_t)len);
    return len;
}

/* Non-blocking: returns whatever the UART RX ring holds (possibly 0, which
 * stdio treats as end of file). The console reads through hal_console_read
 * instead, which calls uart_read directly. */
int _read(int fd, char *buf, int len) { return uart_read(buf, len); }
int _close(int fd) { return -1; }
int _fstat(int fd, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) { return 1; }
int _lseek(int fd, int off, int whence) { return 0; }
int _kill(int pid, int sig)
{
    errno = EINVAL;
    return -1;
}
int _getpid(void) { return 1; }
/* exit()/abort() end up here: there is nothing to return to, so reset. */
void _exit(int status)
{
    NVIC_SystemReset();
    for (;;) {
    }
}

/* EHABI personality routines. Their only references come from the unwind
 * tables of a few libc/libgcc objects (setjmp, __udivmoddi4), which name
 * __aeabi_unwind_cpp_pr0 even though nothing here unwinds: the firmware is C
 * without exceptions, and longjmp restores registers directly. Defining them
 * keeps the libgcc unwinder (unwind-arm.o, pr-support.o, libunwind.o, about
 * 3.5 KB of bank 1) out of the link. Never called. */
/* -fstack-protector-strong (FW_CFLAGS): every function with a local array
 * puts __stack_chk_guard below its saved registers and checks it on return.
 * An overwritten guard calls __stack_chk_fail, which traps (UDF: a
 * UsageFault), so fault_report() records the crash with lr = the return
 * address into the function whose frame was overrun -- before the corrupted
 * return address is used. */
uint32_t __stack_chk_guard = 0xE3A1B5C7u;
__attribute__((noreturn)) void __stack_chk_fail(void);
__attribute__((noreturn)) void __stack_chk_fail(void) { __builtin_trap(); }

void __aeabi_unwind_cpp_pr0(void);
void __aeabi_unwind_cpp_pr1(void);
void __aeabi_unwind_cpp_pr2(void);
void __aeabi_unwind_cpp_pr0(void) {}
void __aeabi_unwind_cpp_pr1(void) {}
void __aeabi_unwind_cpp_pr2(void) {}
