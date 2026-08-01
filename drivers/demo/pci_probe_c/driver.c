#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "pci_probe_c.drv driver_entry()";
static const char probe_message[] OS64_EXPORT = "pci_probe_c.drv pci_read_config32(0,0,0,0)";
static const char bind_message[] OS64_EXPORT = "pci_probe_c.drv bound Bochs VGA";
static const char mmio_message[] OS64_EXPORT = "pci_probe_c.drv mapped BAR capability";

os64_u64 driver_entry(void) {
    os64_klog(entry_message);
    (void)os64_pci_read_config32(0, 0, 0, 0);
    os64_klog(probe_message);
    return 0;
}

os64_u64 driver_probe_pci(const os64_pci_device_info* device) {
    if (device == 0) {
        return 0;
    }
    if (device->vendor_id == 0x1234 && device->device_id == 0x1111) {
        os64_driver_device_handle device_handle;
        if (os64_pci_bind_device_handle(device, 0, &device_handle) == 0) {
            os64_klog(bind_message);
            os64_pci_bar_info bar;
            if (os64_pci_get_bar(device, 0, &bar) == 0 && bar.size != 0) {
                os64_driver_mmio_mapping mapping;
                os64_u64 length = bar.size < 16 ? bar.size : 16;
                if (os64_pci_map_bar_handle(device_handle, 0, 0, length,
                                             OS64_MMIO_DEVICE_UC,
                                             &mapping) == 0) {
                    os64_mmio_barrier(mapping.handle,
                                      OS64_MMIO_BARRIER_FULL);
                    os64_pci_unmap_bar_handle(mapping.handle);
                    os64_klog(mmio_message);
                }
            }
        }
    }
    return 0;
}
