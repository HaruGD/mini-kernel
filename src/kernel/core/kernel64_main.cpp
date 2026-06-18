#include "drivers/gop.h"

static int map_boot_framebuffer(const BootInfo* boot_info) {
    if (boot_info == 0 ||
        boot_info->size < sizeof(BootInfo) ||
        !(boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER) ||
        boot_info->framebuffer_addr == 0 ||
        boot_info->framebuffer_size == 0) {
        return 0;
    }

    uint64_t start = boot_info->framebuffer_addr & ~(PAGING64_PAGE_SIZE - 1ULL);
    uint64_t end = boot_info->framebuffer_addr + boot_info->framebuffer_size;
    end = (end + PAGING64_PAGE_SIZE - 1ULL) & ~(PAGING64_PAGE_SIZE - 1ULL);
    uint64_t flags = PAGING64_FLAG_WRITABLE | PAGING64_FLAG_WRITE_THROUGH | PAGING64_FLAG_CACHE_DISABLE;
    for (uint64_t addr = start; addr < end; addr += PAGING64_PAGE_SIZE) {
        if (!paging64_map_page(addr, addr, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" void kernel64_main(const BootInfo* boot_info) {
    serial_init();
    g_boot_info = boot_info;
    boot_tsc = read_tsc();

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

    pmm64_init(boot_info);
    paging64_init();
    int framebuffer_mapped = map_boot_framebuffer(g_boot_info);
    if (framebuffer_mapped) {
        gop.init_from_boot_info(g_boot_info);
        terminal.init_from_boot_info(g_boot_info);
        terminal.clear();
        early_framebuffer_marker(g_boot_info, 0, 0x000000FF);
    }
    heap_init();
    driver_manager_init();
    driver_manager_register_kernel_exports();
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
    driver_manager_register_builtin_devices();
    vfs_init();
    vfs_mount_fat32_root(root_fat32);
    vfs_mount_memfs("/mem");
    uint32_t autoloaded_drivers = driver_manager_autoload_from("/");
    idt64_init();
    keyboard.init();
    pit.init();
    __asm__ volatile("sti");

    print("Memory ready\n");
    print("Root source: ");
    print(root_from_ramdisk ? "ramdisk" : "ata");
    print("\n");
    print("Framebuffer mapped: ");
    print_hex32((uint32_t)framebuffer_mapped);
    print("\n");
    print("Driver autoloaded: ");
    print_hex32(autoloaded_drivers);
    print("\n");
    print("GDT/TSS ready\n");
    print("Interrupts ready\n");
    print(kernel_shell_prompt());

    while (1) {
        __asm__ volatile("hlt");
    }
}
