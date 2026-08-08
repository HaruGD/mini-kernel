#ifndef OS64_SDK_INTERNAL_H
#define OS64_SDK_INTERNAL_H

#include "os64/syscall_numbers.h"

long os_syscall0(long number);
long os_syscall1(long number, long arg1);
long os_syscall2(long number, long arg1, long arg2);
long os_syscall3(long number, long arg1, long arg2, long arg3);

#endif
