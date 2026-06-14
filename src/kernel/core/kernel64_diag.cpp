static const char* reserved_range_type_name(uint32_t type) {
    if (type == BOOT_RESERVED_RANGE_KERNEL) {
        return "kernel";
    }
    if (type == BOOT_RESERVED_RANGE_BOOT_INFO) {
        return "bootinfo";
    }
    if (type == BOOT_RESERVED_RANGE_PAGE_TABLES) {
        return "page_tables";
    }
    if (type == BOOT_RESERVED_RANGE_FRAMEBUFFER) {
        return "framebuffer";
    }
    if (type == BOOT_RESERVED_RANGE_KERNEL_STACK) {
        return "kernel_stack";
    }
    if (type == BOOT_RESERVED_RANGE_RAMDISK) {
        return "ramdisk";
    }
    return "unknown";
}

static int boot_info_has_reserved_ranges(const BootInfo* boot_info) {
    return boot_info != 0 &&
           boot_info->size >= sizeof(BootInfo) &&
           boot_info->reserved_range_entry_size >= sizeof(BootReservedRange);
}

static uint32_t boot_reserved_range_count(const BootInfo* boot_info) {
    if (!boot_info_has_reserved_ranges(boot_info)) {
        return 0;
    }

    uint32_t count = boot_info->reserved_range_count;
    if (count > BOOT_RESERVED_RANGE_MAX) {
        count = BOOT_RESERVED_RANGE_MAX;
    }
    return count;
}

static void print_reserved_range(const BootReservedRange* range, uint32_t index, int with_pmm_status) {
    print("\nReserved[");
    print_hex32(index);
    print("] ");
    print(reserved_range_type_name(range->type));
    print(" base=");
    print_hex64(range->base);
    print(" size=");
    print_hex64(range->size);
    if (with_pmm_status) {
        print(" pmm=");
        if (range->base >= PMM64_MAX_RAM_SIZE) {
            print("outside");
        } else {
            print(pmm64_range_is_marked_used(range->base, range->size) ? "used" : "FREE");
        }
    }
}

void print_boot_info() {
    if (g_boot_info == 0) {
        print("\nBootInfo: null");
        return;
    }

    print("\nBootInfo magic: ");
    print_hex32(g_boot_info->magic);
    print("\nVersion: ");
    print_hex32(g_boot_info->version);
    print("\nBoot drive: ");
    print_hex32(g_boot_info->boot_drive);
    print("\nKernel load: ");
    print_hex32(g_boot_info->kernel_load_addr);
    print("\nKernel sectors: ");
    print_hex32(g_boot_info->kernel_sector_count);
    print("\nKernel bytes: ");
    print_hex32(g_boot_info->kernel_file_size);
    print("\nStage2 load: ");
    print_hex32(g_boot_info->stage2_load_addr);
    print("\nMemory map addr: ");
    print_hex32(g_boot_info->memory_map_addr);
    print("\nMemory map entries: ");
    print_hex32(g_boot_info->memory_map_entry_count);
    print("\nMemory map entry size: ");
    print_hex32(g_boot_info->memory_map_entry_size);
    print("\nFlags: ");
    print_hex32(g_boot_info->flags);
    print("\nBoot path: ");
    print((g_boot_info->flags & BOOT_INFO_FLAG_UEFI) ? "UEFI" : "BIOS");
    if (g_boot_info->size >= sizeof(BootInfo) &&
        (g_boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER)) {
        print("\nFramebuffer addr: ");
        print_hex64(g_boot_info->framebuffer_addr);
        print("\nFramebuffer bytes: ");
        print_hex64(g_boot_info->framebuffer_size);
        print("\nFramebuffer width: ");
        print_hex32(g_boot_info->framebuffer_width);
        print("\nFramebuffer height: ");
        print_hex32(g_boot_info->framebuffer_height);
        print("\nFramebuffer stride: ");
        print_hex32(g_boot_info->framebuffer_pixels_per_scanline);
        print("\nFramebuffer format: ");
        print_hex32(g_boot_info->framebuffer_format);
    }
    if (g_boot_info->size >= sizeof(BootInfo) &&
        (g_boot_info->flags & BOOT_INFO_FLAG_RAMDISK)) {
        print("\nRamdisk addr: ");
        print_hex64(g_boot_info->ramdisk_addr);
        print("\nRamdisk bytes: ");
        print_hex64(g_boot_info->ramdisk_size);
    }
    if (boot_info_has_reserved_ranges(g_boot_info)) {
        uint32_t count = boot_reserved_range_count(g_boot_info);
        print("\nReserved range count: ");
        print_hex32(count);
        for (uint32_t i = 0; i < count; i++) {
            print_reserved_range(&g_boot_info->reserved_ranges[i], i, 0);
        }
    }
}

void command_memstat() {
    print("\nPMM total pages: ");
    print_hex32(pmm64_get_total_block_count());
    print("\nPMM free pages: ");
    print_hex32(pmm64_get_free_block_count());
    print("\nPaging root: ");
    print_hex64(paging64_get_root_phys());
    print("\nHeap used bytes: ");
    print_hex64(heap_total_used());
    print("\nHeap free bytes: ");
    print_hex64(heap_total_free());
    print("\nHeap mapped bytes: ");
    print_hex64(heap_total_mapped_bytes());
    print("\nHeap mapped pages: ");
    print_hex32(heap_mapped_page_count());
    print("\nHeap regions: ");
    print_hex32(heap_region_count());
    if (boot_info_has_reserved_ranges(g_boot_info)) {
        uint32_t count = boot_reserved_range_count(g_boot_info);
        print("\nBoot reserved ranges: ");
        print_hex32(count);
        for (uint32_t i = 0; i < count; i++) {
            print_reserved_range(&g_boot_info->reserved_ranges[i], i, 1);
        }
    }
}

void command_version() {
    print("\n[OS-Kernel] v0.0.x (64-bit Long Mode, C++)");
    print("\nBoot path: ");
    print((g_boot_info != 0 && (g_boot_info->flags & BOOT_INFO_FLAG_UEFI)) ? "UEFI" : "BIOS");
    print(" + FAT32/VFS loader + IDT/IRQ");
}

void command_uptime() {
    print("\nTick: ");
    print_hex32(pit.get_tick());
    print("\nTSC delta: ");
    print_hex64(read_tsc() - boot_tsc);
}
