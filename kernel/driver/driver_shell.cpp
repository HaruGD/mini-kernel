#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/kutil64.h"
#include "fs/vfs.h"

extern "C" {
    #include "heap.h"
}

static void print_permission_names(uint32_t permissions) {
    if (permissions == 0) {
        print("-");
        return;
    }
    int printed = 0;
    struct PermissionName {
        uint32_t bit;
        const char* name;
    };
    static const PermissionName names[] = {
        {0x00000001, "PCI"},
        {0x00000002, "MMIO"},
        {0x00000004, "INTERRUPT"},
        {0x00000008, "BLOCK"},
        {0x00000010, "VFS"},
        {0x00000020, "INPUT"},
        {0x00000040, "TIMER"},
        {0x00000080, "DISPLAY"},
    };

    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if ((permissions & names[i].bit) == 0) {
            continue;
        }
        if (printed) {
            print("|");
        }
        print(names[i].name);
        printed = 1;
    }
}

static int name_ends_with_drv(const char* name) {
    int len = strlen64(name);
    if (len < 4) {
        return 0;
    }
    return name[len - 4] == '.' &&
           to_lower_ascii(name[len - 3]) == 'd' &&
           to_lower_ascii(name[len - 2]) == 'r' &&
           to_lower_ascii(name[len - 1]) == 'v';
}

void command_drivers() {
    print("\n=== DRIVERS ===");
    uint32_t count = driver_manager_count();
    print("\ncount=");
    print_hex32(count);
    for (uint32_t i = 0; i < count; i++) {
        const DriverRecord* driver = driver_manager_get(i);
        if (driver == 0) {
            continue;
        }
        print("\n[");
        print_hex32(i);
        print("] ");
        print(driver->name);
        print(" kind=");
        print(driver_kind_name(driver->kind));
        print(" state=");
        print(driver_state_name(driver->state));
        print(" perms=");
        print_permission_names(driver->permissions);
    }

    print("\nexports=");
    print_hex32(driver_export_count());
    print(" bindings=");
    print_hex32(driver_manager_binding_count());
    print(" irq_hooks=");
    print_hex32(driver_irq_hook_count());
    print("\n===============");
}

static const char* drv_load_result_name(int result) {
    if (result == DRIVER_LOAD_OK) return "ok";
    if (result == DRIVER_LOAD_BAD_HEADER) return "bad_header";
    if (result == DRIVER_LOAD_UNSUPPORTED_ABI) return "unsupported_abi";
    if (result == DRIVER_LOAD_SIGNATURE_REQUIRED) return "signature_required";
    if (result == DRIVER_LOAD_POLICY_DENIED) return "policy_denied";
    if (result == DRIVER_LOAD_NO_SLOT) return "no_slot";
    if (result == DRIVER_LOAD_DUPLICATE) return "duplicate";
    if (result == DRIVER_LOAD_OUT_OF_MEMORY) return "out_of_memory";
    if (result == DRIVER_LOAD_UNRESOLVED_IMPORT) return "unresolved_import";
    if (result == DRIVER_LOAD_UNSUPPORTED_RELOC) return "unsupported_reloc";
    if (result == DRIVER_LOAD_ENTRY_FAILED) return "entry_failed";
    if (result == DRIVER_LOAD_SIGNATURE_INVALID) return "signature_invalid";
    if (result == DRIVER_LOAD_SIGNATURE_UNSUPPORTED) return "signature_unsupported";
    if (result == DRIVER_LOAD_UNLOAD_DENIED) return "unload_denied";
    if (result == DRIVER_LOAD_EXIT_FAILED) return "exit_failed";
    if (result == DRIVER_LOAD_MISSING_DEPENDENCY) return "missing_dependency";
    if (result == DRIVER_LOAD_BIND_DENIED) return "bind_denied";
    if (result == DRIVER_LOAD_IRQ_DENIED) return "irq_denied";
    return "unknown";
}

void command_bindings() {
    print("\n=== DRIVER BINDINGS ===");
    uint32_t count = driver_manager_binding_count();
    print("\ncount=");
    print_hex32(count);
    for (uint32_t i = 0; i < count; i++) {
        const DriverBindingRecord* binding = driver_manager_binding_get(i);
        if (binding == 0) {
            continue;
        }
        print("\n[");
        print_hex32(i);
        print("] ");
        print(binding->driver);
        print(" pci ");
        print_hex32(binding->bus);
        print(":");
        print_hex32(binding->device);
        print(".");
        print_hex32(binding->function);
        print(" vendor=");
        print_hex32(binding->vendor_id);
        print(" device=");
        print_hex32(binding->device_id);
        print(" class=");
        print_hex32(binding->class_code);
        print(" subclass=");
        print_hex32(binding->subclass);
        print(" prog_if=");
        print_hex32(binding->prog_if);
    }
    print("\n=======================");
}

void command_irqhooks() {
    print("\n=== IRQ HOOKS ===");
    uint32_t count = driver_irq_hook_count();
    print("\ncount=");
    print_hex32(count);
    for (uint32_t i = 0; i < count; i++) {
        const DriverIrqHookRecord* hook = driver_irq_hook_get(i);
        if (hook == 0) {
            continue;
        }
        print("\n[");
        print_hex32(i);
        print("] irq=");
        print_hex32(hook->irq);
        print(" driver=");
        print(hook->driver);
        print(" calls=");
        print_hex64(hook->call_count);
    }
    print("\n=================");
}

static void print_last_driver_error() {
    const DriverLoadDiagnostics* diag = driver_manager_last_error();
    if (diag == 0 || diag->result == DRIVER_LOAD_OK || diag->stage[0] == '\0') {
        return;
    }

    print("\nerror_stage=");
    print(diag->stage);
    if (diag->module[0] != '\0') {
        print(" module=");
        print(diag->module);
    }
    if (diag->name[0] != '\0') {
        print(" name=");
        print(diag->name);
    }
    print(" index=");
    print_hex32(diag->index);
    print(" detail=");
    print_hex64(diag->detail);
}

void command_drvlast() {
    const DriverLoadDiagnostics* diag = driver_manager_last_error();
    print("\n=== DRV LAST ===");
    if (diag == 0 || diag->result == DRIVER_LOAD_OK) {
        print("\nresult=ok");
        print("\n==============");
        return;
    }
    print("\nresult=");
    print(drv_load_result_name(diag->result));
    print_last_driver_error();
    print("\n==============");
}

static void print_drv_manifest(const DrvManifest* manifest) {
    print("\nname=");
    print(manifest->name);
    print(" version=");
    print(manifest->version);
    print(" entry=");
    print(manifest->entry_symbol);
    print("\npermissions=");
    print_permission_names(manifest->permissions);
    print(" boot_modes=");
    print_hex32(manifest->boot_modes);
    print(" deps=");
    print_hex32(manifest->dependency_count);
}

static uint8_t* read_drv_file(const char* target, uint32_t* bytes_out) {
    if (bytes_out != 0) {
        *bytes_out = 0;
    }

    VFSFileInfo info;
    const char* read_target = target;
    char absolute_target[96];
    if (vfs_get_file_info(read_target, &info) != VFS_OK || info.type != VFS_NODE_FILE) {
        if (target == 0 || target[0] == '/' || strlen64(target) + 2 > (int)sizeof(absolute_target)) {
            return 0;
        }
        absolute_target[0] = '/';
        copy_string64(absolute_target + 1, sizeof(absolute_target) - 1, target);
        read_target = absolute_target;
        if (vfs_get_file_info(read_target, &info) != VFS_OK || info.type != VFS_NODE_FILE) {
            return 0;
        }
    }
    if (info.size < sizeof(DrvHeader)) {
        return 0;
    }

    uint8_t* image = (uint8_t*)kmalloc(info.size);
    if (image == 0) {
        return 0;
    }

    uint32_t bytes_read = 0;
    if (vfs_read_file(read_target, image, info.size, &bytes_read) != VFS_OK || bytes_read != info.size) {
        kfree(image);
        return 0;
    }

    if (bytes_out != 0) {
        *bytes_out = bytes_read;
    }
    return image;
}

void command_drvcheck(const char* path) {
    const char* target = (path != 0 && path[0] != '\0') ? path : "hello.drv";
    uint32_t bytes_read = 0;
    uint8_t* image = read_drv_file(target, &bytes_read);
    if (image == 0) {
        print("\nDRV check failed: file not found");
        return;
    }

    int result = driver_manager_validate_drv_image(image, bytes_read);
    print("\nDRV check ");
    print(result == DRIVER_LOAD_OK ? "OK" : "FAILED");
    print(" result=");
    print(drv_load_result_name(result));
    print(" file=");
    print(target);
    print(" bytes=");
    print_hex32(bytes_read);

    if (result == DRIVER_LOAD_OK) {
        const DrvHeader* header = (const DrvHeader*)image;
        const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
        print_drv_manifest(manifest);
        print("\nsections=");
        print_hex32(header->section_count);
        print(" imports=");
        print_hex32(header->import_count);
        print(" exports=");
        print_hex32(header->export_count);
        print(" relocs=");
        print_hex32(header->relocation_count);
        print("\nsignature bytes=");
        print_hex32((uint32_t)header->signature_size);
        print(" cert bytes=");
        print_hex32((uint32_t)header->certificate_size);
    } else {
        print_last_driver_error();
    }

    kfree(image);
}

void command_drvinfo(const char* path) {
    const char* target = (path != 0 && path[0] != '\0') ? path : "hello.drv";
    uint32_t bytes_read = 0;
    uint8_t* image = read_drv_file(target, &bytes_read);
    if (image == 0) {
        print("\nDRV info failed: file not found");
        return;
    }

    int result = driver_manager_validate_drv_image(image, bytes_read);
    print("\n=== DRV INFO ===");
    print("\nfile=");
    print(target);
    print(" result=");
    print(drv_load_result_name(result));
    print(" bytes=");
    print_hex32(bytes_read);
    if (result != DRIVER_LOAD_OK) {
        print_last_driver_error();
        print("\n==============");
        kfree(image);
        return;
    }

    const DrvHeader* header = (const DrvHeader*)image;
    const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
    print_drv_manifest(manifest);
    print("\nsections=");
    print_hex32(header->section_count);
    print(" symbols=");
    print_hex32(header->symbol_count);
    print(" imports=");
    print_hex32(header->import_count);
    print(" exports=");
    print_hex32(header->export_count);
    print(" relocs=");
    print_hex32(header->relocation_count);
    print(" deps=");
    print_hex32(manifest->dependency_count);

    for (uint32_t i = 0; i < manifest->dependency_count; i++) {
        const DrvDependency* dependency = (const DrvDependency*)(image + header->manifest_offset + sizeof(DrvManifest) + (uint64_t)i * sizeof(DrvDependency));
        print("\ndep[");
        print_hex32(i);
        print("] ");
        print(dependency->name);
        print(" min_state=");
        print_hex32(dependency->min_state);
    }

    for (uint32_t i = 0; i < header->import_count; i++) {
        const DrvImport* import = (const DrvImport*)(image + header->import_table_offset + (uint64_t)i * header->import_entry_size);
        print("\nimport[");
        print_hex32(i);
        print("] ");
        print(import->module);
        print(".");
        print(import->name);
        print(" requires=");
        print_permission_names(import->required_permission);
    }
    for (uint32_t i = 0; i < header->export_count; i++) {
        const DrvExport* export_entry = (const DrvExport*)(image + header->export_table_offset + (uint64_t)i * header->export_entry_size);
        print("\nexport[");
        print_hex32(i);
        print("] ");
        print(export_entry->name);
        print(" requires=");
        print_permission_names(export_entry->flags);
    }
    print("\n==============");
    kfree(image);
}

void command_drvload(const char* path) {
    const char* target = (path != 0 && path[0] != '\0') ? path : "hello.drv";
    uint32_t bytes_read = 0;
    uint8_t* image = read_drv_file(target, &bytes_read);
    if (image == 0) {
        print("\nDRV load failed: file not found");
        return;
    }

    int result = driver_manager_load_drv_image(image, bytes_read);
    print("\nDRV load ");
    print((result == DRIVER_LOAD_OK || result == DRIVER_LOAD_DUPLICATE) ? "OK" : "FAILED");
    print(" result=");
    print(drv_load_result_name(result));
    print(" file=");
    print(target);

    if (result == DRIVER_LOAD_OK || result == DRIVER_LOAD_DUPLICATE) {
        const DrvHeader* header = (const DrvHeader*)image;
        const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
        print_drv_manifest(manifest);
    } else {
        print_last_driver_error();
    }

    kfree(image);
}

void command_drvunload(const char* name) {
    const char* target = (name != 0 && name[0] != '\0') ? name : "hello";
    int result = driver_manager_unload(target);
    print("\nDRV unload ");
    print(result == DRIVER_LOAD_OK ? "OK" : "FAILED");
    print(" result=");
    print(drv_load_result_name(result));
    print(" name=");
    print(target);
    if (result != DRIVER_LOAD_OK) {
        print_last_driver_error();
    }
}

void command_drvreload(const char* path) {
    const char* target = (path != 0 && path[0] != '\0') ? path : "hello.drv";
    uint32_t bytes_read = 0;
    uint8_t* image = read_drv_file(target, &bytes_read);
    if (image == 0) {
        print("\nDRV reload failed: file not found");
        return;
    }

    int result = driver_manager_reload_drv_image(image, bytes_read);
    print("\nDRV reload ");
    print(result == DRIVER_LOAD_OK ? "OK" : "FAILED");
    print(" result=");
    print(drv_load_result_name(result));
    print(" file=");
    print(target);

    if (result == DRIVER_LOAD_OK) {
        const DrvHeader* header = (const DrvHeader*)image;
        const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
        print_drv_manifest(manifest);
    } else {
        print_last_driver_error();
    }

    kfree(image);
}

static int load_drv_file_quiet(const char* path) {
    uint32_t bytes_read = 0;
    uint8_t* image = read_drv_file(path, &bytes_read);
    if (image == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    int result = driver_manager_load_drv_image(image, bytes_read);
    kfree(image);
    return result;
}

static void join_path(char* out, uint32_t out_size, const char* dir, const char* name) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (dir == 0 || dir[0] == '\0' || strcmp64(dir, "/") == 0) {
        out[0] = '/';
        copy_string64(out + 1, out_size - 1, name);
        return;
    }
    copy_string64(out, out_size, dir);
    int len = strlen64(out);
    if (len > 0 && out[len - 1] != '/' && (uint32_t)len + 1 < out_size) {
        out[len] = '/';
        out[len + 1] = '\0';
        len++;
    }
    if ((uint32_t)len < out_size) {
        copy_string64(out + len, out_size - (uint32_t)len, name);
    }
}

uint32_t driver_manager_autoload_from(const char* path) {
    const char* dir = (path != 0 && path[0] != '\0') ? path : "/";
    uint32_t loaded = 0;
    for (uint32_t pass = 0; pass < 4; pass++) {
        int progress = 0;
        int fd = vfs_opendir(dir);
        if (fd < 0) {
            return loaded;
        }
        for (;;) {
            VFSDirEntry entry;
            int result = vfs_readdir(fd, &entry);
            if (result <= 0) {
                break;
            }
            if (entry.type != VFS_NODE_FILE || !name_ends_with_drv(entry.name)) {
                continue;
            }
            char full_path[128];
            join_path(full_path, sizeof(full_path), dir, entry.name);
            int load_result = load_drv_file_quiet(full_path);
            if (load_result == DRIVER_LOAD_OK) {
                loaded++;
                progress = 1;
            }
        }
        vfs_closedir(fd);
        if (!progress) {
            break;
        }
    }
    return loaded;
}

void command_drvautoload(const char* path) {
    const char* dir = (path != 0 && path[0] != '\0') ? path : "/";
    uint32_t loaded = driver_manager_autoload_from(dir);
    print("\nDRV autoload path=");
    print(dir);
    print(" loaded=");
    print_hex32(loaded);
}
