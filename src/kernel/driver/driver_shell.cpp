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
    return "unknown";
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
    }

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
    print(result == DRIVER_LOAD_OK ? "OK" : "FAILED");
    print(" result=");
    print(drv_load_result_name(result));
    print(" file=");
    print(target);

    if (result == DRIVER_LOAD_OK || result == DRIVER_LOAD_DUPLICATE) {
        const DrvHeader* header = (const DrvHeader*)image;
        const DrvManifest* manifest = (const DrvManifest*)(image + header->manifest_offset);
        print_drv_manifest(manifest);
    }

    kfree(image);
}
