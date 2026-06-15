#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "pci_probe_c.drv driver_entry()";
static const char probe_message[] OS64_EXPORT = "pci_probe_c.drv pci_read_config32(0,0,0,0)";

os64_u64 driver_entry(void) {
    os64_klog(entry_message);
    (void)os64_pci_read_config32(0, 0, 0, 0);
    os64_klog(probe_message);
    return 0;
}
