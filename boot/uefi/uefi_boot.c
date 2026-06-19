#include <stdint.h>
#include <stddef.h>

#include "kernel/boot_info.h"

#define EFIAPI __attribute__((ms_abi))

typedef uint16_t CHAR16;
typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint8_t UINT8;
typedef int64_t INTN;
typedef uint8_t BOOLEAN;

typedef struct EFI_GUID {
    UINT32 data1;
    uint16_t data2;
    uint16_t data3;
    UINT8 data4[8];
} EFI_GUID;

typedef struct EFI_TABLE_HEADER {
    UINT64 signature;
    UINT32 revision;
    UINT32 header_size;
    UINT32 crc32;
    UINT32 reserved;
} EFI_TABLE_HEADER;

#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL ((EFI_STATUS)0x8000000000000005ULL)
#define EFI_NOT_FOUND        ((EFI_STATUS)0x800000000000000EULL)

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2

#define EFI_LOADER_DATA 2
#define EFI_CONVENTIONAL_MEMORY 7

#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define KERNEL_LOAD_ADDR 0x100000ULL
#define KERNEL_MAX_SIZE  (4ULL * 1024ULL * 1024ULL)
#define KERNEL_MAX_PAGES (KERNEL_MAX_SIZE / PAGE_SIZE)
#define KERNEL_RUNTIME_RESERVE_SIZE (1ULL * 1024ULL * 1024ULL)
#define KERNEL_RUNTIME_RESERVE_PAGES (KERNEL_RUNTIME_RESERVE_SIZE / PAGE_SIZE)
#define RAMDISK_MAX_SIZE (32ULL * 1024ULL * 1024ULL)
#define RAMDISK_MAX_PAGES (RAMDISK_MAX_SIZE / PAGE_SIZE)
#define KERNEL_STACK_PAGES 16
#define PAGE_SIZE 4096ULL

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_BOOT_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* self,
    CHAR16* string);

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* reset;
    EFI_TEXT_STRING output_string;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_MEMORY_DESCRIPTOR {
    UINT32 type;
    UINT32 pad;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    UINT64 number_of_pages;
    UINT64 attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER hdr;
    CHAR16* firmware_vendor;
    UINT32 firmware_revision;
    EFI_HANDLE console_in_handle;
    void* con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* std_err;
    void* runtime_services;
    struct EFI_BOOT_SERVICES* boot_services;
    UINTN number_of_table_entries;
    void* configuration_table;
} EFI_SYSTEM_TABLE;

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
    UINT32 revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE* system_table;
    EFI_HANDLE device_handle;
    void* file_path;
    void* reserved;
    UINT32 load_options_size;
    void* load_options;
    void* image_base;
    UINT64 image_size;
    UINT32 image_code_type;
    UINT32 image_data_type;
    EFI_STATUS (EFIAPI *unload)(EFI_HANDLE image_handle);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL* self,
    EFI_FILE_PROTOCOL** new_handle,
    CHAR16* file_name,
    UINT64 open_mode,
    UINT64 attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL* self);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    EFI_FILE_PROTOCOL* self,
    UINTN* buffer_size,
    void* buffer);

struct EFI_FILE_PROTOCOL {
    UINT64 revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    void* delete_file;
    EFI_FILE_READ read;
    void* write;
    void* get_position;
    void* set_position;
    void* get_info;
    void* set_info;
    void* flush;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 revision;
    EFI_STATUS (EFIAPI *open_volume)(
        struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* self,
        EFI_FILE_PROTOCOL** root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct EFI_PIXEL_BITMASK {
    UINT32 red_mask;
    UINT32 green_mask;
    UINT32 blue_mask;
    UINT32 reserved_mask;
} EFI_PIXEL_BITMASK;

typedef struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    UINT32 version;
    UINT32 horizontal_resolution;
    UINT32 vertical_resolution;
    UINT32 pixel_format;
    EFI_PIXEL_BITMASK pixel_information;
    UINT32 pixels_per_scanline;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    UINT32 max_mode;
    UINT32 mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info;
    UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS framebuffer_base;
    UINTN framebuffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void* query_mode;
    void* set_mode;
    void* blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct FramebufferInfo {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
} FramebufferInfo;

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void* raise_tpl;
    void* restore_tpl;
    EFI_STATUS (EFIAPI *allocate_pages)(
        int type,
        int memory_type,
        UINTN pages,
        EFI_PHYSICAL_ADDRESS* memory);
    EFI_STATUS (EFIAPI *free_pages)(EFI_PHYSICAL_ADDRESS memory, UINTN pages);
    EFI_STATUS (EFIAPI *get_memory_map)(
        UINTN* memory_map_size,
        EFI_MEMORY_DESCRIPTOR* memory_map,
        UINTN* map_key,
        UINTN* descriptor_size,
        UINT32* descriptor_version);
    EFI_STATUS (EFIAPI *allocate_pool)(int pool_type, UINTN size, void** buffer);
    EFI_STATUS (EFIAPI *free_pool)(void* buffer);
    void* create_event;
    void* set_timer;
    void* wait_for_event;
    void* signal_event;
    void* close_event;
    void* check_event;
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    EFI_STATUS (EFIAPI *handle_protocol)(
        EFI_HANDLE handle,
        EFI_GUID* protocol,
        void** interface);
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    void* load_image;
    void* start_image;
    void* exit;
    void* unload_image;
    EFI_STATUS (EFIAPI *exit_boot_services)(EFI_HANDLE image_handle, UINTN map_key);
    void* get_next_monotonic_count;
    EFI_STATUS (EFIAPI *stall)(UINTN microseconds);
    EFI_STATUS (EFIAPI *set_watchdog_timer)(
        UINTN timeout,
        UINT64 watchdog_code,
        UINTN data_size,
        CHAR16* watchdog_data);
    void* connect_controller;
    void* disconnect_controller;
    void* open_protocol;
    void* close_protocol;
    void* open_protocol_information;
    void* protocols_per_handle;
    void* locate_handle_buffer;
    EFI_STATUS (EFIAPI *locate_protocol)(
        EFI_GUID* protocol,
        void* registration,
        void** interface);
    void* install_multiple_protocol_interfaces;
    void* uninstall_multiple_protocol_interfaces;
    void* calculate_crc32;
    void* copy_mem;
    void* set_mem;
    void* create_event_ex;
} EFI_BOOT_SERVICES;

static EFI_GUID loaded_image_guid = {
    0x5B1B31A1, 0x9562, 0x11d2,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static EFI_GUID simple_file_system_guid = {
    0x0964E5B22, 0x6459, 0x11d2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static EFI_GUID graphics_output_guid = {
    0x9042A9DE, 0x23DC, 0x4A38,
    {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}
};

static EFI_SYSTEM_TABLE* g_st = 0;
static EFI_BOOT_SERVICES* g_bs = 0;

extern void uefi_enter_kernel(uint64_t entry, void* boot_info, uint64_t pml4, uint64_t stack_top);

static void* memset64(void* dest, int value, UINTN size) {
    UINT8* out = (UINT8*)dest;
    for (UINTN i = 0; i < size; i++) {
        out[i] = (UINT8)value;
    }
    return dest;
}

static void* memcpy64(void* dest, const void* src, UINTN size) {
    UINT8* out = (UINT8*)dest;
    const UINT8* in = (const UINT8*)src;
    for (UINTN i = 0; i < size; i++) {
        out[i] = in[i];
    }
    return dest;
}

static void puts16(CHAR16* text) {
    if (g_st != 0 && g_st->con_out != 0 && g_st->con_out->output_string != 0) {
        g_st->con_out->output_string(g_st->con_out, text);
    }
}

static void puts_ascii(const char* text) {
    if (text == 0 || g_st == 0 || g_st->con_out == 0 || g_st->con_out->output_string == 0) {
        return;
    }

    CHAR16 buffer[96];
    UINTN index = 0;
    while (*text != 0) {
        buffer[index++] = (CHAR16)(UINT8)*text++;
        if (index == 95 || *text == 0) {
            buffer[index] = 0;
            g_st->con_out->output_string(g_st->con_out, buffer);
            index = 0;
        }
    }
}

static void puts_hex64(UINT64 value) {
    static const char digits[] = "0123456789ABCDEF";
    CHAR16 buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (UINTN i = 0; i < 16; i++) {
        UINTN shift = (15 - i) * 4;
        buffer[2 + i] = (CHAR16)digits[(value >> shift) & 0xFULL];
    }
    buffer[18] = 0;
    puts16(buffer);
}

static void log_step(const char* text) {
    puts_ascii("[uefi] ");
    puts_ascii(text);
    puts_ascii("\r\n");
}

static void log_value(const char* label, UINT64 value) {
    puts_ascii("[uefi] ");
    puts_ascii(label);
    puts_ascii("=");
    puts_hex64(value);
    puts_ascii("\r\n");
}

static void log_status(const char* label, EFI_STATUS status) {
    puts_ascii("[uefi] ");
    puts_ascii(label);
    puts_ascii(" status=");
    puts_hex64(status);
    puts_ascii("\r\n");
}

static void stall_seconds(UINTN seconds) {
    if (g_bs == 0 || g_bs->stall == 0) {
        return;
    }
    for (UINTN i = 0; i < seconds; i++) {
        g_bs->stall(1000000);
    }
}

static EFI_STATUS fail_with_pause(CHAR16* text, EFI_STATUS status) {
    static CHAR16 pause_msg[] = {
        'O','S','6','4',' ','b','o','o','t',' ','s','t','o','p','p','e','d','.','\r','\n',0
    };
    puts16(text);
    puts16(pause_msg);
    stall_seconds(8);
    return status;
}

static int allocate_fixed_pages(EFI_PHYSICAL_ADDRESS addr, UINTN pages) {
    EFI_PHYSICAL_ADDRESS request = addr;
    return g_bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &request) == EFI_SUCCESS &&
           request == addr;
}

static int allocate_any_below_4g(UINTN pages, EFI_PHYSICAL_ADDRESS* out) {
    EFI_PHYSICAL_ADDRESS max = 0xFFFFFFFFULL;
    EFI_STATUS status = g_bs->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, pages, &max);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    *out = max;
    return 1;
}

static EFI_FILE_PROTOCOL* open_root_file(EFI_HANDLE image_handle, CHAR16* primary, CHAR16* fallback) {
    EFI_LOADED_IMAGE_PROTOCOL* loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = 0;
    EFI_FILE_PROTOCOL* root = 0;
    EFI_FILE_PROTOCOL* file = 0;

    if (g_bs->handle_protocol(image_handle, &loaded_image_guid, (void**)&loaded) != EFI_SUCCESS) {
        return 0;
    }
    if (g_bs->handle_protocol(loaded->device_handle, &simple_file_system_guid, (void**)&fs) != EFI_SUCCESS) {
        return 0;
    }
    if (fs->open_volume(fs, &root) != EFI_SUCCESS) {
        return 0;
    }

    if (primary != 0 && root->open(root, &file, primary, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS) {
        return file;
    }
    if (fallback != 0 && root->open(root, &file, fallback, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS) {
        return file;
    }

    return 0;
}

static EFI_FILE_PROTOCOL* open_kernel_file(EFI_HANDLE image_handle) {
    static CHAR16 kernel_bin[] = {
        'K','E','R','N','E','L','.','B','I','N',0
    };
    static CHAR16 kernel64_bin[] = {
        'K','E','R','N','E','L','6','4','.','B','I','N',0
    };
    return open_root_file(image_handle, kernel_bin, kernel64_bin);
}

static EFI_FILE_PROTOCOL* open_ramdisk_file(EFI_HANDLE image_handle) {
    static CHAR16 os64_bin[] = {
        'O','S','6','4','.','B','I','N',0
    };
    return open_root_file(image_handle, os64_bin, 0);
}

static void add_reserved_range(BootInfo* boot_info, uint64_t base, uint64_t size, uint32_t type) {
    if (boot_info == 0 || size == 0 || boot_info->reserved_range_count >= BOOT_RESERVED_RANGE_MAX) {
        return;
    }

    uint32_t index = boot_info->reserved_range_count;
    boot_info->reserved_ranges[index].base = base;
    boot_info->reserved_ranges[index].size = size;
    boot_info->reserved_ranges[index].type = type;
    boot_info->reserved_ranges[index].flags = 0;
    boot_info->reserved_range_count++;
}

static UINTN read_kernel(EFI_FILE_PROTOCOL* file, UINT8* dest) {
    UINTN total = 0;
    while (total < KERNEL_MAX_SIZE) {
        UINTN chunk = 65536;
        if (chunk > KERNEL_MAX_SIZE - total) {
            chunk = KERNEL_MAX_SIZE - total;
        }

        EFI_STATUS status = file->read(file, &chunk, dest + total);
        if (status != EFI_SUCCESS) {
            return 0;
        }
        if (chunk == 0) {
            break;
        }
        total += chunk;
    }
    return total;
}

static UINTN read_file_limited(EFI_FILE_PROTOCOL* file, UINT8* dest, UINTN max_size) {
    UINTN total = 0;
    while (total < max_size) {
        UINTN chunk = 65536;
        if (chunk > max_size - total) {
            chunk = max_size - total;
        }

        EFI_STATUS status = file->read(file, &chunk, dest + total);
        if (status != EFI_SUCCESS) {
            return 0;
        }
        if (chunk == 0) {
            break;
        }
        total += chunk;
    }
    return total;
}

static uint32_t uefi_type_to_e820(uint32_t type) {
    if (type == EFI_CONVENTIONAL_MEMORY) {
        return 1;
    }
    return 2;
}

static uint32_t convert_memory_map(
    EFI_MEMORY_DESCRIPTOR* map,
    UINTN map_size,
    UINTN desc_size,
    E820Entry* e820) {
    uint32_t count = 0;
    UINTN offset = 0;

    while (offset + desc_size <= map_size && count < E820_MAX_ENTRIES) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + offset);
        uint64_t base = desc->physical_start;
        uint64_t length = desc->number_of_pages * PAGE_SIZE;

        if (length != 0) {
            e820[count].base_low = (uint32_t)base;
            e820[count].base_high = (uint32_t)(base >> 32);
            e820[count].length_low = (uint32_t)length;
            e820[count].length_high = (uint32_t)(length >> 32);
            e820[count].type = uefi_type_to_e820(desc->type);
            e820[count].acpi_attrs = 1;
            count++;
        }

        offset += desc_size;
    }

    return count;
}

static void collect_framebuffer_info(FramebufferInfo* fb) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;
    if (fb == 0) {
        return;
    }

    fb->base = 0;
    fb->size = 0;
    fb->width = 0;
    fb->height = 0;
    fb->pixels_per_scanline = 0;
    fb->format = 0;

    if (g_bs->locate_protocol(&graphics_output_guid, 0, (void**)&gop) != EFI_SUCCESS ||
        gop == 0 || gop->mode == 0 || gop->mode->info == 0) {
        return;
    }

    fb->base = gop->mode->framebuffer_base;
    fb->size = gop->mode->framebuffer_size;
    fb->width = gop->mode->info->horizontal_resolution;
    fb->height = gop->mode->info->vertical_resolution;
    fb->pixels_per_scanline = gop->mode->info->pixels_per_scanline;
    fb->format = gop->mode->info->pixel_format;
}

static void map_2mb_page(uint64_t** page_directories, uint64_t phys) {
    uint64_t page_index = phys >> 21;
    uint64_t pdpt_index = page_index >> 9;
    uint64_t pd_index = page_index & 0x1FF;

    if (pdpt_index >= 4 || page_directories[pdpt_index] == 0) {
        return;
    }
    page_directories[pdpt_index][pd_index] = (page_index << 21) | 0x83;
}

static void build_identity_tables(uint64_t table_phys, const FramebufferInfo* fb) {
    uint64_t pml4_phys = table_phys;
    uint64_t pdpt_phys = table_phys + PAGE_SIZE;
    uint64_t pd0_phys = table_phys + PAGE_SIZE * 2;
    uint64_t pd1_phys = table_phys + PAGE_SIZE * 3;
    uint64_t pd2_phys = table_phys + PAGE_SIZE * 4;
    uint64_t pd3_phys = table_phys + PAGE_SIZE * 5;
    uint64_t* pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;
    uint64_t* page_directories[4];
    page_directories[0] = (uint64_t*)(uintptr_t)pd0_phys;
    page_directories[1] = (uint64_t*)(uintptr_t)pd1_phys;
    page_directories[2] = (uint64_t*)(uintptr_t)pd2_phys;
    page_directories[3] = (uint64_t*)(uintptr_t)pd3_phys;

    memset64(pml4, 0, PAGE_SIZE);
    memset64(pdpt, 0, PAGE_SIZE);
    for (uint32_t i = 0; i < 4; i++) {
        memset64(page_directories[i], 0, PAGE_SIZE);
    }

    pml4[0] = pdpt_phys | 0x03;
    pdpt[0] = pd0_phys | 0x03;
    pdpt[1] = pd1_phys | 0x03;
    pdpt[2] = pd2_phys | 0x03;
    pdpt[3] = pd3_phys | 0x03;

    for (uint32_t pdpt_i = 0; pdpt_i < 4; pdpt_i++) {
        for (uint32_t pd_i = 0; pd_i < 512; pd_i++) {
            uint64_t page_index = (uint64_t)pdpt_i * 512 + pd_i;
            page_directories[pdpt_i][pd_i] = (page_index << 21) | 0x83;
        }
    }

    if (fb != 0 && fb->base != 0 && fb->size != 0) {
        uint64_t start = fb->base & ~0x1FFFFFULL;
        uint64_t end = (fb->base + fb->size + 0x1FFFFFULL) & ~0x1FFFFFULL;
        for (uint64_t phys = start; phys < end; phys += 0x200000ULL) {
            map_2mb_page(page_directories, phys);
        }
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    g_st = system_table;
    g_bs = system_table->boot_services;

    static CHAR16 start_msg[] = {'U','E','F','I',' ','l','o','a','d','e','r','\r','\n',0};
    static CHAR16 no_kernel_msg[] = {'K','E','R','N','E','L','.','B','I','N',' ','n','o','t',' ','f','o','u','n','d','\r','\n',0};
    static CHAR16 read_fail_msg[] = {'K','e','r','n','e','l',' ','r','e','a','d',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 kernel_alloc_fail_msg[] = {'K','e','r','n','e','l',' ','a','d','d','r',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 ebs_fail_msg[] = {'E','x','i','t','B','o','o','t','S','e','r','v','i','c','e','s',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 temp_alloc_fail_msg[] = {'K','e','r','n','e','l',' ','t','e','m','p',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 ramdisk_alloc_fail_msg[] = {'R','a','m','d','i','s','k',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 bootinfo_alloc_fail_msg[] = {'B','o','o','t','I','n','f','o',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 table_alloc_fail_msg[] = {'P','a','g','e',' ','t','a','b','l','e',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 stack_alloc_fail_msg[] = {'K','e','r','n','e','l',' ','s','t','a','c','k',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 mmap_alloc_fail_msg[] = {'M','e','m','o','r','y',' ','m','a','p',' ','a','l','l','o','c',' ','f','a','i','l','e','d','\r','\n',0};
    static CHAR16 jump_msg[] = {'E','x','i','t','i','n','g',' ','U','E','F','I',' ','a','n','d',' ','j','u','m','p','i','n','g',' ','i','n',' ','3','s','.','.','.','\r','\n',0};

    puts16(start_msg);
    log_step("loader start");
    g_bs->set_watchdog_timer(0, 0, 0, 0);
    log_step("watchdog disabled");

    EFI_PHYSICAL_ADDRESS kernel_temp_phys = 0;
    if (!allocate_any_below_4g(KERNEL_MAX_PAGES, &kernel_temp_phys)) {
        return fail_with_pause(temp_alloc_fail_msg, EFI_NOT_FOUND);
    }
    log_value("kernel temp", kernel_temp_phys);

    EFI_FILE_PROTOCOL* kernel = open_kernel_file(image_handle);
    if (kernel == 0) {
        return fail_with_pause(no_kernel_msg, EFI_NOT_FOUND);
    }
    log_step("kernel file opened");

    UINTN kernel_size = read_kernel(kernel, (UINT8*)(uintptr_t)kernel_temp_phys);
    kernel->close(kernel);
    if (kernel_size == 0) {
        return fail_with_pause(read_fail_msg, EFI_NOT_FOUND);
    }
    log_value("kernel bytes", kernel_size);

    if (!allocate_fixed_pages(KERNEL_LOAD_ADDR, KERNEL_RUNTIME_RESERVE_PAGES)) {
        return fail_with_pause(kernel_alloc_fail_msg, EFI_NOT_FOUND);
    }
    UINTN kernel_reserved_size = KERNEL_RUNTIME_RESERVE_SIZE;
    memcpy64((void*)(uintptr_t)KERNEL_LOAD_ADDR, (const void*)(uintptr_t)kernel_temp_phys, kernel_size);
    log_value("kernel load", KERNEL_LOAD_ADDR);
    log_value("kernel reserve bytes", kernel_reserved_size);

    EFI_PHYSICAL_ADDRESS ramdisk_phys = 0;
    UINTN ramdisk_size = 0;
    EFI_FILE_PROTOCOL* ramdisk_file = open_ramdisk_file(image_handle);
    if (ramdisk_file != 0) {
        log_step("ramdisk file opened");
        if (!allocate_any_below_4g(RAMDISK_MAX_PAGES, &ramdisk_phys)) {
            return fail_with_pause(ramdisk_alloc_fail_msg, EFI_NOT_FOUND);
        }
        ramdisk_size = read_file_limited(ramdisk_file, (UINT8*)(uintptr_t)ramdisk_phys, RAMDISK_MAX_SIZE);
        ramdisk_file->close(ramdisk_file);
        if (ramdisk_size == 0) {
            ramdisk_phys = 0;
        }
        log_value("ramdisk addr", ramdisk_phys);
        log_value("ramdisk bytes", ramdisk_size);
    } else {
        log_step("ramdisk file missing");
    }

    FramebufferInfo framebuffer_info;
    collect_framebuffer_info(&framebuffer_info);
    log_value("gop fb", framebuffer_info.base);
    log_value("gop fb bytes", framebuffer_info.size);
    log_value("gop width", framebuffer_info.width);
    log_value("gop height", framebuffer_info.height);

    EFI_PHYSICAL_ADDRESS boot_info_phys = BOOT_INFO_ADDR;
    if (!allocate_fixed_pages(boot_info_phys, 1)) {
        if (!allocate_any_below_4g(1, &boot_info_phys)) {
            return fail_with_pause(bootinfo_alloc_fail_msg, EFI_NOT_FOUND);
        }
    }
    log_value("bootinfo addr", boot_info_phys);

    EFI_PHYSICAL_ADDRESS table_phys = 0x5000;
    uint64_t pml4_phys = 0x5000;
    if (!allocate_fixed_pages(table_phys, 6)) {
        if (!allocate_any_below_4g(6, &table_phys)) {
            return fail_with_pause(table_alloc_fail_msg, EFI_NOT_FOUND);
        }
        pml4_phys = table_phys;
    }
    build_identity_tables(table_phys, &framebuffer_info);
    log_value("pml4 addr", pml4_phys);

    EFI_PHYSICAL_ADDRESS kernel_stack_phys = 0;
    if (!allocate_any_below_4g(KERNEL_STACK_PAGES, &kernel_stack_phys)) {
        return fail_with_pause(stack_alloc_fail_msg, EFI_NOT_FOUND);
    }
    uint64_t kernel_stack_size = KERNEL_STACK_PAGES * PAGE_SIZE;
    uint64_t kernel_stack_top = kernel_stack_phys + kernel_stack_size;
    log_value("kernel stack base", kernel_stack_phys);
    log_value("kernel stack top", kernel_stack_top);

    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    g_bs->get_memory_map(&map_size, 0, &map_key, &desc_size, &desc_version);
    UINTN map_capacity = map_size + desc_size * 128;
    log_value("mmap initial bytes", map_size);
    log_value("mmap desc bytes", desc_size);
    log_value("mmap capacity", map_capacity);

    EFI_MEMORY_DESCRIPTOR* memory_map = 0;
    if (g_bs->allocate_pool(EFI_LOADER_DATA, map_capacity, (void**)&memory_map) != EFI_SUCCESS) {
        return fail_with_pause(mmap_alloc_fail_msg, EFI_NOT_FOUND);
    }
    log_step("memory map buffer allocated");

    puts16(jump_msg);
    stall_seconds(3);

    BootInfo* boot_info = (BootInfo*)(uintptr_t)boot_info_phys;
    E820Entry* e820 = (E820Entry*)((UINT8*)boot_info + sizeof(BootInfo));

    EFI_STATUS status = EFI_NOT_FOUND;
    log_step("enter ExitBootServices loop");
    for (uint32_t attempt = 0; attempt < 5; attempt++) {
        log_value("exit attempt", attempt);
        map_size = map_capacity;
        status = g_bs->get_memory_map(&map_size, memory_map, &map_key, &desc_size, &desc_version);
        if (status != EFI_SUCCESS) {
            log_status("get_memory_map", status);
            break;
        }

        uint32_t e820_count = convert_memory_map(memory_map, map_size, desc_size, e820);

        boot_info->magic = BOOT_INFO_MAGIC;
        boot_info->version = BOOT_INFO_VERSION;
        boot_info->size = sizeof(BootInfo);
        boot_info->boot_drive = 0xFFFFFFFFU;
        boot_info->kernel_load_addr = (uint32_t)KERNEL_LOAD_ADDR;
        boot_info->kernel_sector_count = (uint32_t)((kernel_size + 511) / 512);
        boot_info->kernel_file_size = (uint32_t)kernel_size;
        boot_info->stage2_load_addr = 0;
        boot_info->memory_map_addr = (uint32_t)(boot_info_phys + sizeof(BootInfo));
        boot_info->memory_map_entry_count = e820_count;
        boot_info->memory_map_entry_size = sizeof(E820Entry);
        boot_info->flags = BOOT_INFO_FLAG_UEFI;
        boot_info->framebuffer_addr = framebuffer_info.base;
        boot_info->framebuffer_size = framebuffer_info.size;
        boot_info->framebuffer_width = framebuffer_info.width;
        boot_info->framebuffer_height = framebuffer_info.height;
        boot_info->framebuffer_pixels_per_scanline = framebuffer_info.pixels_per_scanline;
        boot_info->framebuffer_format = framebuffer_info.format;
        boot_info->reserved_range_count = 0;
        boot_info->reserved_range_entry_size = sizeof(BootReservedRange);
        boot_info->ramdisk_addr = ramdisk_phys;
        boot_info->ramdisk_size = ramdisk_size;
        for (uint32_t i = 0; i < BOOT_RESERVED_RANGE_MAX; i++) {
            boot_info->reserved_ranges[i].base = 0;
            boot_info->reserved_ranges[i].size = 0;
            boot_info->reserved_ranges[i].type = 0;
            boot_info->reserved_ranges[i].flags = 0;
        }
        add_reserved_range(boot_info, KERNEL_LOAD_ADDR, kernel_reserved_size, BOOT_RESERVED_RANGE_KERNEL);
        add_reserved_range(boot_info, boot_info_phys, PAGE_SIZE, BOOT_RESERVED_RANGE_BOOT_INFO);
        add_reserved_range(boot_info, table_phys, PAGE_SIZE * 6, BOOT_RESERVED_RANGE_PAGE_TABLES);
        add_reserved_range(boot_info, kernel_stack_phys, kernel_stack_size, BOOT_RESERVED_RANGE_KERNEL_STACK);
        if (framebuffer_info.base != 0 && framebuffer_info.size != 0) {
            boot_info->flags |= BOOT_INFO_FLAG_FRAMEBUFFER;
            add_reserved_range(boot_info, framebuffer_info.base, framebuffer_info.size, BOOT_RESERVED_RANGE_FRAMEBUFFER);
        }
        if (ramdisk_phys != 0 && ramdisk_size != 0) {
            boot_info->flags |= BOOT_INFO_FLAG_RAMDISK;
            add_reserved_range(boot_info, ramdisk_phys, ramdisk_size, BOOT_RESERVED_RANGE_RAMDISK);
        }

        status = g_bs->exit_boot_services(image_handle, map_key);
        if (status == EFI_SUCCESS) {
            break;
        }
        log_status("ExitBootServices retry", status);
    }

    if (status != EFI_SUCCESS) {
        return fail_with_pause(ebs_fail_msg, status);
    }

    uefi_enter_kernel(KERNEL_LOAD_ADDR, boot_info, pml4_phys, kernel_stack_top);

    return EFI_SUCCESS;
}
