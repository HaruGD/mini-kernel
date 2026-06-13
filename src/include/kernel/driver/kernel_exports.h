#ifndef KERNEL_DRIVER_KERNEL_EXPORTS_H
#define KERNEL_DRIVER_KERNEL_EXPORTS_H

#include <stdint.h>

extern "C" void driver_klog(const char* text);
extern "C" void* driver_kmalloc(uint64_t size);
extern "C" void driver_kfree(void* ptr);

#endif
