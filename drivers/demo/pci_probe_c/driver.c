#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "pci_probe_c.drv driver_entry()";
static const char probe_message[] OS64_EXPORT = "pci_probe_c.drv pci_read_config32(0,0,0,0)";
static const char bind_message[] OS64_EXPORT = "pci_probe_c.drv bound Bochs VGA";
static const char mmio_message[] OS64_EXPORT = "pci_probe_c.drv mapped BAR capability";
static const char dma_message[] OS64_EXPORT = "pci_probe_c.drv coherent DMA capability OK";
static const char stream_message[] OS64_EXPORT = "pci_probe_c.drv streaming DMA capability OK";
static const char qemu_dma_message[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU DMA round trip OK";
static const char qemu_irq_message[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU IRQ completion OK";
static const char qemu_stream_message[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU streaming DMA round trip OK";
static const char qemu_sg_message[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU SG DMA round trip OK";
static const char qemu_bind_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU bind denied";
static const char qemu_mmio_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU MMIO denied";
static const char qemu_domain_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU DMA domain denied";
static const char qemu_source_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU source allocation denied";
static const char qemu_destination_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU destination allocation denied";
static const char qemu_irq_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU IRQ registration denied";
static const char qemu_bus_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU bus mastering denied";
static const char qemu_irq_completion_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU IRQ completion timeout";
static const char qemu_irq_mmio_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU IRQ MMIO denied";
static const char qemu_irq_status_failure[] OS64_EXPORT = "pci_probe_c.drv QEMU EDU IRQ status empty";

static os64_driver_mmio_handle edu_irq_mmio;
static volatile os64_u32 edu_irq_count;
static volatile os64_u32 edu_irq_mmio_denied;
static volatile os64_u32 edu_irq_status_empty;

static os64_u64 edu_irq_handler(os64_u64 irq) {
    os64_u64 status = 0;
    (void)irq;
    int result = os64_mmio_read(edu_irq_mmio, 0x24, 4, &status);
    if (result == 0 && status != 0) {
        os64_mmio_write(edu_irq_mmio, 0x64, 4, status);
        edu_irq_count++;
    } else if (result != 0) edu_irq_mmio_denied++;
    else edu_irq_status_empty++;
    return 0;
}

static int wait_edu_dma(os64_driver_mmio_handle mmio) {
    for (os64_u32 spin = 0; spin < 1000000u; spin++) {
        os64_u64 command = 0;
        if (os64_mmio_read(mmio, 0x98, 4, &command) != 0) return 0;
        if ((command & 1u) == 0) return 1;
    }
    return 0;
}

static int wait_edu_irq(os64_driver_mmio_handle mmio, os64_u32 before) {
    for (os64_u32 spin = 0; spin < 32u; spin++) {
        os64_u64 status = 0;
        if (edu_irq_count != before) return 1;
        /* Yield to interrupt delivery without allowing this sleepable probe
         * invocation to be preempted in the middle of its kernel call. */
        if (os64_irq_wait_once() != 0) return 0;
        if (os64_mmio_read(mmio, 0x24, 4, &status) != 0) return 0;
        if (edu_irq_count != before) return 1;
    }
    return 0;
}

static int run_edu_dma(os64_driver_mmio_handle mmio, os64_u64 source,
                       os64_u64 destination, os64_u64 count,
                       os64_u32 command, int expect_irq) {
    os64_u32 before = edu_irq_count;
    if (os64_mmio_write(mmio, 0x80, 8, source) != 0 ||
        os64_mmio_write(mmio, 0x88, 8, destination) != 0 ||
        os64_mmio_write(mmio, 0x90, 8, count) != 0 ||
        os64_mmio_write(mmio, 0x98, 4, command) != 0 ||
        !wait_edu_dma(mmio)) return 0;
    if (!expect_irq) return 1;
    return wait_edu_irq(mmio, before);
}

static int raise_edu_irq(os64_driver_mmio_handle mmio) {
    os64_u32 before = edu_irq_count;
    /* QEMU EDU's dedicated interrupt-raise register avoids its 100 ms DMA
     * completion timer making a kernel-side driver self-test timing-sensitive. */
    if (os64_mmio_write(mmio, 0x60, 4, 0x200u) != 0) return 0;
    return wait_edu_irq(mmio, before);
}

static void probe_qemu_edu(const os64_pci_device_info* device) {
    os64_driver_device_handle dev;
    if (os64_pci_bind_device_handle(device, 0, &dev) != 0) {
        os64_klog(qemu_bind_failure);
        return;
    }
    os64_driver_mmio_mapping mmio;
    if (os64_pci_map_bar_handle(dev, 0, 0, 0x100,
                                 OS64_MMIO_DEVICE_UC, &mmio) != 0) {
        os64_klog(qemu_mmio_failure);
        return;
    }
    os64_driver_dma_domain_handle domain;
    if (os64_dma_prepare_device(dev, OS64_DMA_TRUSTED_DIRECT, &domain) != 0 ||
        os64_dma_set_mask(dev, 32) != 0) {
        os64_klog(qemu_domain_failure);
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    os64_driver_dma_buffer source;
    if (os64_dma_alloc_coherent(dev, 4096, 4096, 0, &source) != 0) {
        os64_klog(qemu_source_failure);
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    os64_driver_dma_buffer destination;
    if (os64_dma_alloc_coherent(dev, 4096, 4096, 0, &destination) != 0) {
        os64_klog(qemu_destination_failure);
        os64_dma_free_coherent(source.handle);
        os64_pci_unmap_bar_handle(mmio.handle);
        return;
    }
    for (os64_u32 i = 0; i < 512; i++)
        ((unsigned char*)source.cpu_address)[i] = (unsigned char)(i ^ 0xA5u);
    edu_irq_mmio = mmio.handle;
    edu_irq_count = 0;
    edu_irq_mmio_denied = 0;
    edu_irq_status_empty = 0;
    os64_mmio_write(mmio.handle, 0x64, 4, 0xFFFFFFFFu);
    int irq_registered = device->irq_line < 16 &&
        os64_irq_register(device->irq_line, edu_irq_handler) == 0;
    if (!irq_registered) os64_klog(qemu_irq_failure);
    int passed = 0;
    int irq_passed = 0;
    int bus_ready = os64_dma_enable_bus_mastering(dev) == 0;
    if (!bus_ready) os64_klog(qemu_bus_failure);
    irq_passed = bus_ready && irq_registered && raise_edu_irq(mmio.handle);
    if (bus_ready && irq_registered && !irq_passed) {
        os64_klog(qemu_irq_completion_failure);
        if (edu_irq_mmio_denied != 0) os64_klog(qemu_irq_mmio_failure);
        if (edu_irq_status_empty != 0) os64_klog(qemu_irq_status_failure);
    }
    int interrupt_dma = bus_ready &&
        run_edu_dma(mmio.handle, source.dma_address.value,
                    0x40000, 512, 1, 0);
    if (interrupt_dma &&
        run_edu_dma(mmio.handle, 0x40000,
                    destination.dma_address.value, 512, 3, 0)) {
        passed = 1;
        for (os64_u32 i = 0; i < 512; i++) {
            if (((unsigned char*)destination.cpu_address)[i] !=
                (unsigned char)(i ^ 0xA5u)) {
                passed = 0;
                break;
            }
        }
    }

    int stream_passed = 0;
    os64_driver_allocation stream_target;
    if (passed && os64_drv_alloc(4096, 4096,
                                 OS64_DRV_ALLOC_ZERO | OS64_DRV_ALLOC_PAGES,
                                 "edu_stream", &stream_target) == 0) {
        os64_driver_dma_mapping stream;
        if (os64_dma_map_buffer(dev, stream_target.handle, 0, 512,
                                OS64_DMA_FROM_DEVICE, &stream) == 0) {
            os64_u64 offset = 0;
            stream_passed = 1;
            for (os64_u32 segment = 0; segment < stream.segment_count;
                 segment++) {
                if (!run_edu_dma(mmio.handle, 0x40000 + offset,
                                 stream.segments[segment].address.value,
                                 stream.segments[segment].length, 3, 0)) {
                    stream_passed = 0;
                    break;
                }
                offset += stream.segments[segment].length;
            }
            if (offset != 512 || os64_dma_sync_for_cpu(stream.handle) != 0)
                stream_passed = 0;
            if (stream_passed)
                for (os64_u32 i = 0; i < 512; i++)
                    if (((unsigned char*)stream_target.address)[i] !=
                        (unsigned char)(i ^ 0xA5u)) stream_passed = 0;
            if (os64_dma_unmap(stream.handle) != 0) stream_passed = 0;
        }
        os64_drv_free(stream_target.handle);
    }

    int sg_passed = 0;
    os64_driver_allocation sg_a = {0}, sg_b = {0};
    if (stream_passed &&
        os64_drv_alloc(4096, 4096,
                       OS64_DRV_ALLOC_ZERO | OS64_DRV_ALLOC_PAGES,
                       "edu_sg_a", &sg_a) == 0 &&
        os64_drv_alloc(4096, 4096,
                       OS64_DRV_ALLOC_ZERO | OS64_DRV_ALLOC_PAGES,
                       "edu_sg_b", &sg_b) == 0) {
        for (os64_u32 i = 0; i < 256; i++) {
            ((unsigned char*)sg_a.address)[i] = (unsigned char)(i ^ 0x3Cu);
            ((unsigned char*)sg_b.address)[i] = (unsigned char)(i ^ 0xC3u);
        }
        os64_driver_dma_source sources[2] = {
            {sg_a.handle, 0, 256}, {sg_b.handle, 0, 256}
        };
        os64_driver_dma_mapping sg;
        if (os64_dma_map_sg(dev, sources, 2, OS64_DMA_TO_DEVICE, &sg) == 0) {
            os64_u64 offset = 0;
            sg_passed = 1;
            for (os64_u32 segment = 0; segment < sg.segment_count; segment++) {
                if (!run_edu_dma(mmio.handle,
                                 sg.segments[segment].address.value,
                                 0x40000 + offset,
                                 sg.segments[segment].length, 1, 0)) {
                    sg_passed = 0;
                    break;
                }
                offset += sg.segments[segment].length;
            }
            if (os64_dma_unmap(sg.handle) != 0 || offset != 512 ||
                !run_edu_dma(mmio.handle, 0x40000,
                             destination.dma_address.value, 512, 3, 0))
                sg_passed = 0;
            if (sg_passed)
                for (os64_u32 i = 0; i < 512; i++) {
                    unsigned char expected = i < 256
                        ? (unsigned char)(i ^ 0x3Cu)
                        : (unsigned char)((i - 256) ^ 0xC3u);
                    if (((unsigned char*)destination.cpu_address)[i] != expected)
                        sg_passed = 0;
                }
        }
    }
    if (sg_b.handle.generation) os64_drv_free(sg_b.handle);
    if (sg_a.handle.generation) os64_drv_free(sg_a.handle);
    os64_dma_disable_bus_mastering(dev);
    if (irq_registered)
        os64_irq_unregister(device->irq_line, edu_irq_handler);
    os64_dma_free_coherent(destination.handle);
    os64_dma_free_coherent(source.handle);
    os64_pci_unmap_bar_handle(mmio.handle);
    if (passed) os64_klog(qemu_dma_message);
    if (irq_passed) os64_klog(qemu_irq_message);
    if (stream_passed) os64_klog(qemu_stream_message);
    if (sg_passed) os64_klog(qemu_sg_message);
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
            os64_driver_mmio_mapping mapping;
            if (os64_pci_map_bar_handle(device_handle, 0, 0, 16,
                                         OS64_MMIO_DEVICE_UC,
                                         &mapping) == 0) {
                os64_mmio_barrier(mapping.handle,
                                  OS64_MMIO_BARRIER_FULL);
                os64_pci_unmap_bar_handle(mapping.handle);
                os64_klog(mmio_message);
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
