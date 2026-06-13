extern "C" void kernel64_main(const BootInfo* boot_info) {
    serial_init();

    g_boot_info = boot_info;
    early_framebuffer_marker(g_boot_info, 0, 0x000000FF);
    terminal.init_from_boot_info(g_boot_info);
    terminal.clear();
    early_framebuffer_marker(g_boot_info, 0, 0x000000FF);
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
        print("\n");
    } else {
        print("BootInfo invalid\n");
    }

    pmm64_init(boot_info);
    early_framebuffer_marker(g_boot_info, 1, 0x0000FF00);
    paging64_init();
    heap_init();
    early_framebuffer_marker(g_boot_info, 2, 0x00FF0000);
    driver_manager_init();
    driver_manager_register_kernel_exports();
    gdt64_init();
    ata.init();
    fat32.init();
    driver_manager_register_builtin_devices();
    vfs_init();
    vfs_mount_fat32_root(&fat32);
    vfs_mount_memfs("/mem");
    idt64_init();
    keyboard.init();
    pit.init();
    __asm__ volatile("sti");

    print("Memory ready\n");
    print("GDT/TSS ready\n");
    print("Interrupts ready\n");
    print(kernel_shell_prompt());

    while (1) {
        __asm__ volatile("hlt");
    }
}
