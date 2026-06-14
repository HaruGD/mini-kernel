#include "os64_driver.h"

typedef os64_u64 (*provider_ping_fn)(void);

extern provider_ping_fn provider_c__provider_ping;

static const char entry_message[] OS64_EXPORT = "consumer_c.drv driver_entry()";

os64_u64 driver_entry(void) {
    os64_klog(entry_message);
    return provider_c__provider_ping();
}
