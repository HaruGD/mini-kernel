#include "drivers/gop.h"
#include "kernel/handle/kernel_objects.h"
#include "kernel/input/input_events.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"

extern "C" uint8_t __kernel_text_start[];
extern "C" uint8_t __kernel_text_end[];
extern "C" uint8_t __kernel_rodata_start[];
extern "C" uint8_t __kernel_rodata_end[];
extern "C" uint8_t __kernel_data_start[];
extern "C" uint8_t __kernel_data_end[];
extern "C" uint8_t __kernel_bss_start[];
extern "C" uint8_t __kernel_bss_end[];

static int remap_range_identity(uint64_t start, uint64_t end, uint64_t flags) {
    if (end <= start) {
        return 1;
    }
    return vm_map_identity(start, end - start, flags);
}

static void drain_kernel_shell_input() {
    keyboard.drain_deferred();
    if (display_session_gui_active() || current_process() != 0 ||
        process_focused() != 0) {
        return;
    }
    OsInputEvent event;
    while (input_events_pop(&event)) {
        if (event.type == OS_INPUT_EVENT_KEY &&
            event.data.key.type == OS_KEY_EVENT_DOWN &&
            event.data.key.character != 0) {
            shell_input((char)event.data.key.character);
        }
    }
}

static int protect_boot_reserved_ranges(const BootInfo* boot_info) {
    if (boot_info == 0 || boot_info->size < sizeof(BootInfo)) {
        return 0;
    }

    int ok = 1;
    for (uint32_t i = 0; i < boot_info->reserved_range_count; i++) {
        const BootReservedRange* range = &boot_info->reserved_ranges[i];
        if (range->type == BOOT_RESERVED_RANGE_KERNEL ||
            range->type == BOOT_RESERVED_RANGE_FRAMEBUFFER) {
            continue;
        }
        uint64_t flags = VM_FLAG_NO_EXECUTE;
        if (range->type == BOOT_RESERVED_RANGE_PAGE_TABLES ||
            range->type == BOOT_RESERVED_RANGE_KERNEL_STACK ||
            range->type == BOOT_RESERVED_RANGE_RAMDISK ||
            range->type == BOOT_RESERVED_RANGE_BOOT_INFO) {
            flags |= VM_FLAG_WRITABLE;
        }
        if (!remap_range_identity(range->base, range->base + range->size, flags)) {
            ok = 0;
        }
    }
    return ok;
}

static int apply_kernel_nx_policy(const BootInfo* boot_info) {
    vm_enable_execute_disable();
    if (!vm_is_execute_disable_enabled()) {
        return 0;
    }

    int ok = 1;
    ok &= remap_range_identity((uint64_t)(uintptr_t)__kernel_text_start,
                               (uint64_t)(uintptr_t)__kernel_text_end,
                               0);
    ok &= remap_range_identity((uint64_t)(uintptr_t)__kernel_rodata_start,
                               (uint64_t)(uintptr_t)__kernel_rodata_end,
                               VM_FLAG_NO_EXECUTE);
    ok &= remap_range_identity((uint64_t)(uintptr_t)__kernel_data_start,
                               (uint64_t)(uintptr_t)__kernel_data_end,
                               VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE);
    ok &= remap_range_identity((uint64_t)(uintptr_t)__kernel_bss_start,
                               (uint64_t)(uintptr_t)__kernel_bss_end,
                               VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE);
    ok &= protect_boot_reserved_ranges(boot_info);
    return ok;
}

static int map_boot_framebuffer(const BootInfo* boot_info) {
    if (boot_info == 0 ||
        boot_info->size < sizeof(BootInfo) ||
        !(boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER) ||
        boot_info->framebuffer_addr == 0 ||
        boot_info->framebuffer_size == 0) {
        return 0;
    }

    uint64_t flags = VM_FLAG_WRITABLE |
                     VM_FLAG_WRITE_THROUGH |
                     VM_FLAG_CACHE_DISABLE |
                     VM_FLAG_NO_EXECUTE;
    return vm_map_identity(boot_info->framebuffer_addr, boot_info->framebuffer_size, flags);
}

extern "C" void kernel64_main(const BootInfo* boot_info) {
    serial_init();
    klog_init();
    g_boot_info = boot_info;
    boot_tsc = read_tsc();
    klog_write(KLOG_INFO, "boot", "kernel entry");

    print("Long mode OK\n");
    if (g_boot_info != 0 && g_boot_info->magic == BOOT_INFO_MAGIC) {
        print("BootInfo magic: ");
        print_hex32(g_boot_info->magic);
        print("\nKernel load: ");
        print_hex32(g_boot_info->kernel_load_addr);
        print("\nKernel sectors: ");
        print_hex32(g_boot_info->kernel_sector_count);
        print("\nKernel bytes: ");
        print_hex32(g_boot_info->kernel_file_size);
        print("\nE820 entries: ");
        print_hex32(g_boot_info->memory_map_entry_count);
        print("\nBoot flags: ");
        print_hex32(g_boot_info->flags);
        print("\nBoot path: ");
        print((g_boot_info->flags & BOOT_INFO_FLAG_UEFI) ? "UEFI" : "BIOS");
        if (g_boot_info->size >= sizeof(BootInfo) &&
            (g_boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER)) {
            print("\nFramebuffer: ");
            print_hex32(g_boot_info->framebuffer_width);
            print("x");
            print_hex32(g_boot_info->framebuffer_height);
            print(" stride=");
            print_hex32(g_boot_info->framebuffer_pixels_per_scanline);
            print(" fmt=");
            print_hex32(g_boot_info->framebuffer_format);
        }
        if (g_boot_info->size >= sizeof(BootInfo) &&
            (g_boot_info->flags & BOOT_INFO_FLAG_RAMDISK)) {
            print("\nRamdisk: ");
            print_hex64(g_boot_info->ramdisk_addr);
            print(" bytes=");
            print_hex64(g_boot_info->ramdisk_size);
        }
        print("\n");
    } else {
        print("BootInfo invalid\n");
    }

    pmm_init(boot_info);
    vm_init();
    int diagnostic_mode = g_boot_info != 0 &&
        (g_boot_info->flags & BOOT_INFO_FLAG_DIAGNOSTIC);
    uint64_t acpi_rsdp = g_boot_info != 0 && g_boot_info->size >= sizeof(BootInfo)
        ? g_boot_info->acpi_rsdp_addr
        : 0;
    int acpi_ready = acpi_init(acpi_rsdp);
    int cpu_topology_ready =
        cpu_topology_init(acpi_state(), cpu_arch_bootstrap_apic_id());
    int cpu_local_ready = cpu_topology_ready && cpu_local_system_init();
    int nx_policy_applied = apply_kernel_nx_policy(g_boot_info);
    int framebuffer_mapped = map_boot_framebuffer(g_boot_info);
    if (framebuffer_mapped) {
        gop.init_from_boot_info(g_boot_info);
        terminal.init_from_boot_info(g_boot_info);
        terminal.clear();
        early_framebuffer_marker(g_boot_info, 0, 0x000000FF);
    }
    heap_init();
    kernel_objects_init();
    int gop_back_buffer_ready = gop.init_back_buffer();
    display_backend_init();
    driver_manager_init();
    driver_allocation_init();
    service_registry_init();
    process_system_init();
    driver_manager_register_kernel_exports();
    driver_manager_activate_linked_boot();
    uint32_t boot_packaged_drivers = diagnostic_mode
        ? 0
        : driver_manager_activate_packaged_boot(g_boot_info);
    pci_discover();
    gdt64_init();
    ata.init();
    uint32_t root_from_ramdisk = 0;
    if (g_boot_info != 0 &&
        g_boot_info->size >= sizeof(BootInfo) &&
        (g_boot_info->flags & BOOT_INFO_FLAG_RAMDISK) &&
        g_boot_info->ramdisk_addr != 0 &&
        g_boot_info->ramdisk_size >= 512) {
        ramdisk_fat32.attach_ramdisk((uint8_t*)(uintptr_t)g_boot_info->ramdisk_addr,
                                     (uint32_t)g_boot_info->ramdisk_size);
        ramdisk_fat32.init();
        root_fat32 = &ramdisk_fat32;
        root_from_ramdisk = ramdisk_fat32.ready() ? 1 : 0;
    }
    if (!root_from_ramdisk) {
        fat32.init();
        root_fat32 = &fat32;
    }
    vfs_init();
    vfs_mount_fat32_root(root_fat32);
    vfs_mount_memfs("/mem");
    uint32_t kernel_packaged_drivers = diagnostic_mode
        ? 0
        : driver_manager_activate_packaged_kernel();
    idt64_init();
    int apic_ready = interrupt_controller_init(acpi_state());
    input_events_init();
    keyboard.init();
    pit.init();
    int smp_ready = apic_ready && smp_start_application_processors();
    driver_manager_activate_linked_kernel();
    __asm__ volatile("sti");
    driver_manager_activate_linked_runtime();
    uint32_t runtime_packaged_drivers = diagnostic_mode
        ? 0
        : driver_manager_activate_packaged_runtime();
    int smp_execution_ready =
        smp_ready && smp_release_scheduler_execution();
    uint32_t autoloaded_drivers = boot_packaged_drivers +
        kernel_packaged_drivers + runtime_packaged_drivers;

    print("Memory ready\n");
    print("Root source: ");
    print(root_from_ramdisk ? "ramdisk" : "ata");
    print("\n");
    print("Framebuffer mapped: ");
    print_hex32((uint32_t)framebuffer_mapped);
    print("\n");
    print("GOP back buffer: ");
    print_hex32((uint32_t)gop_back_buffer_ready);
    print("\n");
    print("NX policy: ");
    print_hex32((uint32_t)nx_policy_applied);
    print("\n");
    print("Driver autoloaded: ");
    print_hex32(autoloaded_drivers);
    print("\n");
    print("Diagnostic mode: ");
    print_hex32((uint32_t)diagnostic_mode);
    print("\nACPI ready: ");
    print_hex32((uint32_t)acpi_ready);
    print("\nCPU topology ready: ");
    print_hex32((uint32_t)cpu_topology_ready);
    print("\nCPU local ready: ");
    print_hex32((uint32_t)cpu_local_ready);
    print("\nInterrupt controller: ");
    print(interrupt_controller_name());
    print(" ready=");
    print_hex32((uint32_t)apic_ready);
    print("\nSMP startup ready: ");
    print_hex32((uint32_t)smp_ready);
    print("\nSMP execution ready: ");
    print_hex32((uint32_t)smp_execution_ready);
    print("\n");
    print("GDT/TSS ready\n");
    print("Interrupts ready\n");
    klog_write(KLOG_INFO, "boot", "kernel initialization complete");
    if (diagnostic_mode) {
        cpu_print_summary();
        cpu_local_print_summary();
        smp_print_summary();
        klog_write(KLOG_INFO, "boot", "diagnostic report follows");
        print_boot_info();
        acpi_print_summary();
        interrupt_controller_print();
        command_pci();
    }
    print(kernel_shell_prompt());

    while (1) {
        __asm__ volatile("cli" : : : "memory");
        drain_kernel_shell_input();
        if (continue_ready_processes(0)) {
            __asm__ volatile("sti" : : : "memory");
            continue;
        }
        shell_publish_prompt_if_pending();
        // The kernel shell is the single-CPU idle context. Interrupts wake
        // this loop, which schedules any process made ready by timer, input,
        // IPC, or child completion before sleeping again.
        __asm__ volatile("sti; hlt" : : : "memory");
    }
}
