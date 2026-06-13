#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/kernel_exports.h"
#include "kernel/kutil64.h"
#include <stddef.h>

extern "C" {
    #include "heap.h"
}

extern "C" void driver_klog(const char* text) {
    print("\n[drv] ");
    print(text != 0 ? text : "(null)");
}

extern "C" void* driver_kmalloc(uint64_t size) {
    return kmalloc((size_t)size);
}

extern "C" void driver_kfree(void* ptr) {
    kfree(ptr);
}

void driver_manager_register_kernel_exports() {
    driver_export_register("kernel", "klog", (void*)driver_klog, 0);
    driver_export_register("kernel", "kmalloc", (void*)driver_kmalloc, 0);
    driver_export_register("kernel", "kfree", (void*)driver_kfree, 0);
}
