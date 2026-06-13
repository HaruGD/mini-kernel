#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"

extern "C" {
    #include "heap.h"
}

static const uint32_t DRV_KNOWN_PERMISSIONS =
    DRV_PERMISSION_PCI |
    DRV_PERMISSION_MMIO |
    DRV_PERMISSION_INTERRUPT |
    DRV_PERMISSION_BLOCK |
    DRV_PERMISSION_VFS |
    DRV_PERMISSION_INPUT |
    DRV_PERMISSION_TIMER;

static int range_inside(uint64_t offset, uint64_t size, uint64_t file_size) {
    if (size == 0) {
        return 1;
    }
    if (offset > file_size) {
        return 0;
    }
    if (size > file_size - offset) {
        return 0;
    }
    return 1;
}

static int table_shape_valid(uint64_t offset,
                             uint32_t count,
                             uint32_t entry_size,
                             uint32_t expected_size,
                             uint64_t file_size) {
    if (count == 0) {
        return 1;
    }
    if (entry_size < expected_size) {
        return 0;
    }
    return range_inside(offset, (uint64_t)count * entry_size, file_size);
}

static void bytes_copy(uint8_t* dst, const uint8_t* src, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static void bytes_zero(uint8_t* dst, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static const DrvSection* drv_section_at(const uint8_t* image, const DrvHeader* header, uint32_t index) {
    return (const DrvSection*)(image + header->section_table_offset + (uint64_t)index * header->section_entry_size);
}

static const DrvSymbol* drv_symbol_at(const uint8_t* image, const DrvHeader* header, uint32_t index) {
    return (const DrvSymbol*)(image + header->symbol_table_offset + (uint64_t)index * header->symbol_entry_size);
}

static const DrvImport* drv_import_at(const uint8_t* image, const DrvHeader* header, uint32_t index) {
    return (const DrvImport*)(image + header->import_table_offset + (uint64_t)index * header->import_entry_size);
}

static void free_loaded_image(DriverLoadedImage* loaded) {
    if (loaded == 0) {
        return;
    }
    for (uint32_t i = 0; i < loaded->section_count && i < DRIVER_MAX_LOADED_SECTIONS; i++) {
        if (loaded->sections[i].base != 0) {
            delete[] loaded->sections[i].base;
            loaded->sections[i].base = 0;
        }
    }
    delete loaded;
}

static int validate_section_ranges(const uint8_t* image, const DrvHeader* header, uint64_t size) {
    if (header->section_count > DRIVER_MAX_LOADED_SECTIONS) {
        return DRIVER_LOAD_POLICY_DENIED;
    }
    for (uint32_t i = 0; i < header->section_count; i++) {
        const DrvSection* section = drv_section_at(image, header, i);
        if (section->memory_size < section->file_size) {
            return DRIVER_LOAD_BAD_HEADER;
        }
        if (section->kind != DRV_SECTION_BSS &&
            !range_inside(section->file_offset, section->file_size, size)) {
            return DRIVER_LOAD_BAD_HEADER;
        }
        if (section->kind == DRV_SECTION_BSS && section->file_size != 0) {
            return DRIVER_LOAD_BAD_HEADER;
        }
    }
    return DRIVER_LOAD_OK;
}

int driver_manager_validate_drv_image(const uint8_t* image, uint64_t size) {
    if (image == 0 || size < sizeof(DrvHeader)) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    const DrvHeader* header = (const DrvHeader*)image;
    if (header->magic != DRV_MAGIC || header->file_size != size) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (header->format_version != DRV_FORMAT_VERSION ||
        header->abi_version != DRV_ABI_VERSION) {
        return DRIVER_LOAD_UNSUPPORTED_ABI;
    }
    if (!range_inside(header->manifest_offset, header->manifest_size, size) ||
        header->manifest_size < sizeof(DrvManifest)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!table_shape_valid(header->section_table_offset,
                           header->section_count,
                           header->section_entry_size,
                           sizeof(DrvSection),
                           size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!table_shape_valid(header->symbol_table_offset,
                           header->symbol_count,
                           header->symbol_entry_size,
                           sizeof(DrvSymbol),
                           size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!table_shape_valid(header->import_table_offset,
                           header->import_count,
                           header->import_entry_size,
                           sizeof(DrvImport),
                           size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!table_shape_valid(header->export_table_offset,
                           header->export_count,
                           header->export_entry_size,
                           sizeof(DrvExport),
                           size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!table_shape_valid(header->relocation_table_offset,
                           header->relocation_count,
                           header->relocation_entry_size,
                           sizeof(DrvRelocation),
                           size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!range_inside(header->signature_offset, header->signature_size, size) ||
        !range_inside(header->certificate_offset, header->certificate_size, size)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (header->signature_size == 0 || header->certificate_size == 0) {
        return DRIVER_LOAD_SIGNATURE_REQUIRED;
    }

    int section_result = validate_section_ranges(image, header, size);
    if (section_result != DRIVER_LOAD_OK) {
        return section_result;
    }

    return DRIVER_LOAD_OK;
}

static int load_sections(const uint8_t* image, const DrvHeader* header, DriverLoadedImage* loaded) {
    loaded->section_count = header->section_count;
    for (uint32_t i = 0; i < header->section_count; i++) {
        const DrvSection* section = drv_section_at(image, header, i);
        uint64_t alloc_size = section->memory_size;
        if (alloc_size == 0) {
            alloc_size = 1;
        }

        uint8_t* memory = new uint8_t[(size_t)alloc_size];
        if (memory == 0) {
            return DRIVER_LOAD_OUT_OF_MEMORY;
        }

        bytes_zero(memory, alloc_size);
        if (section->file_size != 0) {
            bytes_copy(memory, image + section->file_offset, section->file_size);
        }

        loaded->sections[i].base = memory;
        loaded->sections[i].size = alloc_size;
        loaded->sections[i].kind = section->kind;
        copy_string64(loaded->sections[i].name, sizeof(loaded->sections[i].name), section->name);
    }
    return DRIVER_LOAD_OK;
}

static void* resolve_symbol_address(const uint8_t* image,
                                    const DrvHeader* header,
                                    DriverLoadedImage* loaded,
                                    const char* name) {
    for (uint32_t i = 0; i < header->symbol_count; i++) {
        const DrvSymbol* symbol = drv_symbol_at(image, header, i);
        if (strcmp64(symbol->name, name) != 0) {
            continue;
        }
        if (symbol->section_index >= loaded->section_count) {
            return 0;
        }
        if (symbol->value > loaded->sections[symbol->section_index].size) {
            return 0;
        }
        return loaded->sections[symbol->section_index].base + symbol->value;
    }
    return 0;
}

static int resolve_imports(const uint8_t* image,
                           const DrvHeader* header,
                           const DrvManifest* manifest,
                           DriverLoadedImage* loaded) {
    if (header->import_count > DRIVER_MAX_RESOLVED_IMPORTS) {
        return DRIVER_LOAD_POLICY_DENIED;
    }
    loaded->import_count = header->import_count;
    for (uint32_t i = 0; i < header->import_count; i++) {
        const DrvImport* import = drv_import_at(image, header, i);
        void* address = driver_export_resolve(import->module, import->name, manifest->permissions);
        if (address == 0) {
            return DRIVER_LOAD_UNRESOLVED_IMPORT;
        }
        loaded->imports[i].address = address;
        loaded->imports[i].required_permission = import->required_permission;
        copy_string64(loaded->imports[i].module, sizeof(loaded->imports[i].module), import->module);
        copy_string64(loaded->imports[i].name, sizeof(loaded->imports[i].name), import->name);
    }
    return DRIVER_LOAD_OK;
}

static int patch_imports_into_code(const uint8_t* image,
                                   const DrvHeader* header,
                                   DriverLoadedImage* loaded) {
    if (loaded == 0 || loaded->section_count == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    DriverLoadedSection* code = 0;
    for (uint32_t i = 0; i < loaded->section_count; i++) {
        if (loaded->sections[i].kind == DRV_SECTION_CODE) {
            code = &loaded->sections[i];
            break;
        }
    }
    if (code == 0 || code->base == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    for (uint32_t i = 0; i < header->import_count; i++) {
        const DrvImport* import = drv_import_at(image, header, i);
        if (i >= loaded->import_count) {
            return DRIVER_LOAD_BAD_HEADER;
        }
        if (import->patch_offset > code->size || code->size - import->patch_offset < sizeof(uint64_t)) {
            return DRIVER_LOAD_BAD_HEADER;
        }

        uint64_t address = (uint64_t)loaded->imports[i].address;
        uint8_t* patch = code->base + import->patch_offset;
        for (uint32_t byte = 0; byte < sizeof(uint64_t); byte++) {
            patch[byte] = (uint8_t)((address >> (byte * 8u)) & 0xFFu);
        }
    }

    return DRIVER_LOAD_OK;
}

typedef uint64_t (*DriverEntryFn)();

static int call_driver_entry(const DrvManifest* manifest, DriverLoadedImage* loaded) {
    if (manifest == 0 || loaded == 0 || loaded->entry == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }

    DriverEntryFn entry = (DriverEntryFn)loaded->entry;
    uint64_t entry_result = entry();
    if (entry_result != 0) {
        driver_manager_set_state(manifest->name, DRIVER_STATE_FAILED);
        return DRIVER_LOAD_ENTRY_FAILED;
    }

    driver_manager_set_state(manifest->name, DRIVER_STATE_READY);
    return DRIVER_LOAD_OK;
}

int driver_manager_load_drv_image(const uint8_t* image, uint64_t size) {
    int validation = driver_manager_validate_drv_image(image, size);
    if (validation != DRIVER_LOAD_OK) {
        return validation;
    }

    const DrvHeader* header = (const DrvHeader*)image;
    const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
    if (manifest->name[0] == '\0' || manifest->entry_symbol[0] == '\0') {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if ((manifest->permissions & ~DRV_KNOWN_PERMISSIONS) != 0) {
        return DRIVER_LOAD_POLICY_DENIED;
    }
    if ((manifest->boot_modes & DRV_BOOT_NORMAL) == 0) {
        return DRIVER_LOAD_POLICY_DENIED;
    }

    DriverLoadedImage* loaded = new DriverLoadedImage;
    if (loaded == 0) {
        return DRIVER_LOAD_OUT_OF_MEMORY;
    }
    bytes_zero((uint8_t*)loaded, sizeof(DriverLoadedImage));
    uint8_t registered = 0;

    int result = load_sections(image, header, loaded);
    if (result == DRIVER_LOAD_OK) {
        result = resolve_imports(image, header, manifest, loaded);
    }
    if (result == DRIVER_LOAD_OK) {
        loaded->entry = resolve_symbol_address(image, header, loaded, manifest->entry_symbol);
        if (loaded->entry == 0) {
            result = DRIVER_LOAD_BAD_HEADER;
        }
    }
    if (result == DRIVER_LOAD_OK && header->relocation_count != 0) {
        result = DRIVER_LOAD_UNSUPPORTED_RELOC;
    }
    if (result == DRIVER_LOAD_OK) {
        result = patch_imports_into_code(image, header, loaded);
    }
    if (result == DRIVER_LOAD_OK) {
        result = driver_manager_register_package_manifest(manifest, loaded);
        if (result == DRIVER_LOAD_OK) {
            registered = 1;
        }
    }
    if (result == DRIVER_LOAD_OK) {
        result = call_driver_entry(manifest, loaded);
    }
    if (result != DRIVER_LOAD_OK && !registered) {
        free_loaded_image(loaded);
    }

    return result;
}
