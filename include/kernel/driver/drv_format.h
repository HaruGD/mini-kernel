#ifndef KERNEL_DRIVER_DRV_FORMAT_H
#define KERNEL_DRIVER_DRV_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define DRV_MAGIC 0x5652443436534FULL
#define DRV_FORMAT_VERSION 1
#define DRV_ABI_VERSION 1
#define DRV_ARCH_X86_64 0x00008664U

#define DRV_HEADER_SIZE 160
#define DRV_MANIFEST_SIZE 96
#define DRV_SECTION_SIZE 56
#define DRV_SYMBOL_SIZE 72
#define DRV_IMPORT_SIZE 96
#define DRV_EXPORT_SIZE 64
#define DRV_RELOCATION_SIZE 32
#define DRV_DEPENDENCY_SIZE 40
#define DRV_SIGNATURE_SIZE 56
#define DRV_CERTIFICATE_SIZE 48

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
#define DRV_PERMISSION_DISPLAY   0x00000080

#define DRV_BOOT_NORMAL   0x00000001
#define DRV_BOOT_SAFE     0x00000002
#define DRV_BOOT_RECOVERY 0x00000004

#define DRV_DEP_REQUIRED 0x00000001

#define DRV_SIGNATURE_MAGIC 0x314749533436534FULL
#define DRV_CERTIFICATE_MAGIC 0x315452433436534FULL
#define DRV_SIGNATURE_VERSION 1
#define DRV_SIGNATURE_HASH_CHECKSUM64 1
#define DRV_SIGNATURE_ALG_LOCAL_TEST 1
#define DRV_SIGNATURE_ALG_ROOT_KEY 2
#define DRV_SIGNATURE_ALG_TPM_LOCAL 3
#define DRV_CERTIFICATE_TYPE_LOCAL_TEST 1
#define DRV_CERTIFICATE_TYPE_ROOT_KEY 2
#define DRV_CERTIFICATE_TYPE_TPM_LOCAL 3

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

struct DrvDependency {
    char name[32];
    uint32_t flags;
    uint32_t min_state;
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

struct DrvSignatureBlock {
    uint64_t magic;
    uint32_t version;
    uint32_t algorithm;
    uint32_t hash_algorithm;
    uint32_t flags;
    uint64_t signed_size;
    uint64_t signed_hash;
    uint64_t signature_value;
    uint64_t reserved;
} __attribute__((packed));

struct DrvCertificateBlock {
    uint64_t magic;
    uint32_t version;
    uint32_t certificate_type;
    char key_id[32];
} __attribute__((packed));

#ifdef __cplusplus
static_assert(sizeof(DrvHeader) == DRV_HEADER_SIZE, "DrvHeader ABI size changed");
static_assert(sizeof(DrvManifest) == DRV_MANIFEST_SIZE, "DrvManifest ABI size changed");
static_assert(sizeof(DrvSection) == DRV_SECTION_SIZE, "DrvSection ABI size changed");
static_assert(sizeof(DrvSymbol) == DRV_SYMBOL_SIZE, "DrvSymbol ABI size changed");
static_assert(sizeof(DrvImport) == DRV_IMPORT_SIZE, "DrvImport ABI size changed");
static_assert(sizeof(DrvExport) == DRV_EXPORT_SIZE, "DrvExport ABI size changed");
static_assert(sizeof(DrvRelocation) == DRV_RELOCATION_SIZE, "DrvRelocation ABI size changed");
static_assert(sizeof(DrvDependency) == DRV_DEPENDENCY_SIZE, "DrvDependency ABI size changed");
static_assert(sizeof(DrvSignatureBlock) == DRV_SIGNATURE_SIZE, "DrvSignatureBlock ABI size changed");
static_assert(sizeof(DrvCertificateBlock) == DRV_CERTIFICATE_SIZE, "DrvCertificateBlock ABI size changed");
static_assert(offsetof(DrvHeader, signature_offset) == 128, "DrvHeader signature_offset moved");
static_assert(offsetof(DrvManifest, permissions) == 80, "DrvManifest permissions moved");
static_assert(offsetof(DrvImport, patch_offset) == 88, "DrvImport patch_offset moved");
#endif

#endif
