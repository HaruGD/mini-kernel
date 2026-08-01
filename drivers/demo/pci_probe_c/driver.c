#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "pci_probe_c.drv driver_entry()";
static const char probe_message[] OS64_EXPORT = "pci_probe_c.drv pci_read_config32(0,0,0,0)";
static const char bind_message[] OS64_EXPORT = "pci_probe_c.drv bound Bochs VGA";
static const char mmio_message[] OS64_EXPORT = "pci_probe_c.drv mapped BAR capability";
static const char dma_message[] OS64_EXPORT = "pci_probe_c.drv coherent DMA capability OK";
static const char stream_message[] OS64_EXPORT = "pci_probe_c.drv streaming DMA capability OK";
static const char qemu_dma_message[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU DMA round trip OK";

static int wait_edu_dma(os64_driver_mmio_handle mmio) {
    for (os64_u32 spin = 0; spin < 1000000u; spin++) {
        os64_u64 command = 0;
        if (os64_mmio_read(mmio, 0x98, 4, &command) != 0) return 0;
        if ((command & 1u) == 0) return 1;
    }
    return 0;
}

static void probe_qemu_edu(const os64_pci_device_info* device) {
    os64_driver_device_handle dev;
    if (os64_pci_bind_device_handle(device, 0, &dev) != 0) return;
    os64_driver_mmio_mapping mmio;
    if (os64_pci_map_bar_handle(dev, 0, 0, 0x100,
                                 OS64_MMIO_DEVICE_UC, &mmio) != 0) return;
    os64_driver_dma_domain_handle domain;
    if (os64_dma_prepare_device(dev, OS64_DMA_TRUSTED_DIRECT, &domain) != 0 ||
        os64_dma_set_mask(dev, 32) != 0) {
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    os64_driver_dma_buffer source;
    if (os64_dma_alloc_coherent(dev, 4096, 4096, 0, &source) != 0) {
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    os64_driver_dma_buffer destination;
    if (os64_dma_alloc_coherent(dev, 4096, 4096, 0, &destination) != 0) {
        os64_dma_free_coherent(source.handle);
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    for (os64_u32 i = 0; i < 256; i++)
        ((unsigned char*)source.cpu_address)[i] = (unsigned char)(i ^ 0xA5u);
    int passed = 0;
    if (os64_dma_enable_bus_mastering(dev) == 0 &&
        os64_mmio_write(mmio.handle, 0x80, 8, source.dma_address.value) == 0 &&
        os64_mmio_write(mmio.handle, 0x88, 8, 0x40000) == 0 &&
        os64_mmio_write(mmio.handle, 0x90, 8, 256) == 0 &&
        os64_mmio_write(mmio.handle, 0x98, 4, 1) == 0 &&
        wait_edu_dma(mmio.handle) &&
        os64_mmio_write(mmio.handle, 0x80, 8, 0x40000) == 0 &&
        os64_mmio_write(mmio.handle, 0x88, 8,
                        destination.dma_address.value) == 0 &&
        os64_mmio_write(mmio.handle, 0x90, 8, 256) == 0 &&
        os64_mmio_write(mmio.handle, 0x98, 4, 3) == 0 &&
        wait_edu_dma(mmio.handle)) {
        passed = 1;
        for (os64_u32 i = 0; i < 256; i++) {
            if (((unsigned char*)destination.cpu_address)[i] !=
                (unsigned char)(i ^ 0xA5u)) {
                passed = 0;
                break;
            }
        }
    }
    os64_dma_disable_bus_mastering(dev);
    os64_dma_free_coherent(destination.handle);
    os64_dma_free_coherent(source.handle);
    os64_pci_unmap_bar_handle(mmio.handle);
    if (passed) os64_klog(qemu_dma_message);
}

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
    if (device->vendor_id == 0x1234 && device->device_id == 0x11E8) {
        probe_qemu_edu(device);
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
            os64_driver_dma_domain_handle domain;
            if (os64_dma_prepare_device(device_handle,
                                         OS64_DMA_TRUSTED_DIRECT,
                                         &domain) == 0 &&
                os64_dma_set_mask(device_handle, 32) == 0) {
                os64_driver_dma_buffer buffer;
                if (os64_dma_alloc_coherent(device_handle, 4096, 4096, 0,
                                             &buffer) == 0) {
                    ((volatile unsigned char*)buffer.cpu_address)[0] = 0x5A;
                    os64_dma_free_coherent(buffer.handle);
                    os64_klog(dma_message);
                }
                os64_driver_allocation source;
                if (os64_drv_alloc(5000, 4096,
                                   OS64_DRV_ALLOC_ZERO | OS64_DRV_ALLOC_PAGES,
                                   "dma_source", &source) == 0) {
                    os64_driver_dma_mapping stream;
                    if (os64_dma_map_buffer(device_handle, source.handle,
                                            0, 5000,
                                            OS64_DMA_FROM_DEVICE,
                                            &stream) == 0 &&
                        os64_dma_sync_for_cpu(stream.handle) == 0 &&
                        os64_dma_unmap(stream.handle) == 0) {
                        os64_klog(stream_message);
                    }
                    os64_drv_free(source.handle);
                }
            }
        }
    }
    return 0;
}
