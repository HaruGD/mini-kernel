#include "os64_driver.h"

static const char hello_message[] OS64_EXPORT = "hello_c.drv driver_entry()";

os64_u64 driver_entry(void) {
    os64_klog(hello_message);
    return 0;
}
