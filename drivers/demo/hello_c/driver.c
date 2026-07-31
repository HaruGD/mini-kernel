#include "os64_driver.h"

static const char hello_message[] OS64_EXPORT = "hello_c.drv driver_entry()";

os64_u64 driver_entry(void) {
    os64_driver_allocation allocation;
    if (os64_drv_alloc(128, 16, OS64_DRV_ALLOC_ZERO,
                       "hello-scratch", &allocation) != 0) {
        return 1;
    }
    ((volatile unsigned char*)allocation.address)[0] = 0x47;
    if (os64_drv_free(allocation.handle) != 0) {
        return 1;
    }
    os64_klog(hello_message);
    return 0;
}
