#ifndef KERNEL_DRIVER_DRV_FORMAT_H
#define KERNEL_DRIVER_DRV_FORMAT_H

#include <stdint.h>

#define DRV_MAGIC 0x5652443436534FULL
#define DRV_FORMAT_VERSION 1
#define DRV_ABI_VERSION 1

#define DRV_SECTION_CODE 1
#define DRV_SECTION_RODATA 2
#define DRV_SECTION_DATA 3
#define DRV_SECTION_BSS 4

#define DRV_SYMBOL_FUNC 1
#define DRV_SYMBOL_OBJECT 2

#define DRV_RELOC_ABS64 1
#define DRV_RELOC_REL32 2

#define DRV_PERMISSION_PCI       0x00000001
#define DRV_PERMISSION_MMIO      0x00000002
#define DRV_PERMISSION_INTERRUPT 0x00000004
#define DRV_PERMISSION_BLOCK     0x00000008
#define DRV_PERMISSION_VFS       0x00000010
#define DRV_PERMISSION_INPUT     0x00000020
#define DRV_PERMISSION_TIMER     0x00000040

#define DRV_BOOT_NORMAL   0x00000001
#define DRV_BOOT_SAFE     0x00000002
#define DRV_BOOT_RECOVERY 0x00000004

struct DrvHeader {
    uint64_t magic;
    uint32_t format_version;
    uint32_t abi_version;
    uint32_t arch;
    uint32_t flags;
    uint64_t file_size;
    uint64_t manifest_offset;
    uint64_t manifest_size;
    uint64_t section_table_offset;
    uint32_t section_count;
    uint32_t section_entry_size;
    uint64_t symbol_table_offset;
    uint32_t symbol_count;
    uint32_t symbol_entry_size;
    uint64_t import_table_offset;
    uint32_t import_count;
    uint32_t import_entry_size;
    uint64_t export_table_offset;
    uint32_t export_count;
    uint32_t export_entry_size;
    uint64_t relocation_table_offset;
    uint32_t relocation_count;
    uint32_t relocation_entry_size;
    uint64_t signature_offset;
    uint64_t signature_size;
    uint64_t certificate_offset;
    uint64_t certificate_size;
} __attribute__((packed));

struct DrvManifest {
    char name[32];
    char version[16];
    char entry_symbol[32];
    uint32_t permissions;
    uint32_t boot_modes;
    uint32_t flags;
    uint32_t dependency_count;
} __attribute__((packed));

struct DrvSection {
    char name[16];
    uint32_t kind;
    uint32_t flags;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

struct DrvSymbol {
    char name[48];
    uint32_t kind;
    uint32_t section_index;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct DrvImport {
    char module[32];
    char name[48];
    uint32_t required_permission;
    uint32_t flags;
    uint64_t patch_offset;
} __attribute__((packed));

struct DrvExport {
    char name[48];
    uint32_t kind;
    uint32_t flags;
    uint64_t address;
} __attribute__((packed));

struct DrvRelocation {
    uint32_t type;
    uint32_t section_index;
    uint64_t offset;
    uint64_t symbol_index;
    int64_t addend;
} __attribute__((packed));

#endif
