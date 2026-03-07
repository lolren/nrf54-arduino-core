/*
 * System call stubs for newlib.
 *
 * Required for Arduino core builds on ARM Cortex-M.
 * Licensed under the Apache License 2.0
 */

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cmsis.h"

void _exit(int status)
{
    (void)status;
    while (1) {
        __WFI();
    }
}

pid_t _getpid(void)
{
    return 1;
}

int _kill(pid_t pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

extern char __heap_start__;
extern char __heap_end__;
static char *heap_end = &__heap_start__;

caddr_t _sbrk(int incr)
{
    if ((heap_end + incr) > &__heap_end__) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    char *prev_heap_end = heap_end;
    heap_end += incr;
    return (caddr_t)prev_heap_end;
}

size_t nrf54l15_heap_free_bytes(void)
{
    if (heap_end >= &__heap_end__) {
        return 0U;
    }
    return (size_t)(&__heap_end__ - heap_end);
}

int _write(int file, const char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int file)
{
    (void)file;
    errno = EBADF;
    return -1;
}

off_t _lseek(int file, off_t ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    if (st == NULL) {
        errno = EINVAL;
        return -1;
    }

    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}
