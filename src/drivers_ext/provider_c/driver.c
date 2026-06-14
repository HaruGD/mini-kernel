#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "provider_c.drv driver_entry()";
static const char ping_message[] OS64_EXPORT = "provider_c.drv provider_ping()";

os64_u64 provider_ping(void) {
    os64_klog(ping_message);
    return 0;
}

os64_u64 driver_entry(void) {
    os64_klog(entry_message);
    return 0;
}
