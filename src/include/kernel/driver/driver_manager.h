#ifndef KERNEL_DRIVER_MANAGER_H
#define KERNEL_DRIVER_MANAGER_H

#include <stdint.h>

#define DRIVER_MAX_RECORDS 32
#define DRIVER_MAX_EXPORTS 64

#define DRIVER_KIND_CORE   1
#define DRIVER_KIND_BUS    2
#define DRIVER_KIND_BLOCK  3
#define DRIVER_KIND_INPUT  4
#define DRIVER_KIND_TIMER  5
#define DRIVER_KIND_FS     6
#define DRIVER_KIND_DISPLAY 7
#define DRIVER_KIND_MODULE 8

#define DRIVER_STATE_EMPTY      0
#define DRIVER_STATE_REGISTERED 1
#define DRIVER_STATE_READY      2
#define DRIVER_STATE_FAILED     3
#define DRIVER_STATE_REJECTED   4

#define DRIVER_LOAD_OK                 0
#define DRIVER_LOAD_BAD_HEADER        -1
#define DRIVER_LOAD_UNSUPPORTED_ABI   -2
#define DRIVER_LOAD_SIGNATURE_REQUIRED -3
#define DRIVER_LOAD_POLICY_DENIED     -4
#define DRIVER_LOAD_NO_SLOT           -5
#define DRIVER_LOAD_DUPLICATE         -6
#define DRIVER_LOAD_OUT_OF_MEMORY     -7
#define DRIVER_LOAD_UNRESOLVED_IMPORT -8
#define DRIVER_LOAD_UNSUPPORTED_RELOC -9
#define DRIVER_LOAD_ENTRY_FAILED     -10
#define DRIVER_LOAD_SIGNATURE_INVALID -11

#define DRIVER_MAX_LOADED_SECTIONS 16
#define DRIVER_MAX_RESOLVED_IMPORTS 32

struct DrvManifest;

struct DriverLoadedSection {
    uint8_t* base;
    uint64_t size;
    uint32_t kind;
    char name[16];
};

struct DriverResolvedImport {
    void* address;
    uint32_t required_permission;
    char module[32];
    char name[48];
};

struct DriverLoadedImage {
    uint32_t section_count;
    uint32_t import_count;
    DriverLoadedSection sections[DRIVER_MAX_LOADED_SECTIONS];
    DriverResolvedImport imports[DRIVER_MAX_RESOLVED_IMPORTS];
    void* entry;
};

struct DriverRecord {
    uint8_t active;
    uint8_t state;
    uint16_t kind;
    uint32_t permissions;
    char name[32];
    char version[16];
    void* instance;
};

struct DriverExportRecord {
    uint8_t active;
    uint32_t required_permission;
    char module[32];
    char name[48];
    void* address;
};

struct DriverLoadDiagnostics {
    int32_t result;
    uint32_t index;
    uint64_t detail;
    char stage[32];
    char module[32];
    char name[48];
};

void driver_manager_init();
int driver_manager_register_builtin(const char* name,
                                    const char* version,
                                    uint32_t kind,
                                    uint32_t permissions,
                                    void* instance);
int driver_manager_register_package_manifest(const DrvManifest* manifest, void* instance);
int driver_manager_set_state(const char* name, uint32_t state);
uint32_t driver_manager_count();
const DriverRecord* driver_manager_get(uint32_t index);
const char* driver_state_name(uint32_t state);
const char* driver_kind_name(uint32_t kind);
void driver_manager_clear_last_error();
void driver_manager_set_last_error(int result,
                                   const char* stage,
                                   const char* module,
                                   const char* name,
                                   uint32_t index,
                                   uint64_t detail);
const DriverLoadDiagnostics* driver_manager_last_error();

int driver_export_register(const char* module,
                           const char* name,
                           void* address,
                           uint32_t required_permission);
void* driver_export_resolve(const char* module, const char* name, uint32_t granted_permissions);
uint32_t driver_export_count();
const DriverExportRecord* driver_export_get(uint32_t index);

int driver_manager_validate_drv_image(const uint8_t* image, uint64_t size);
int driver_manager_load_drv_image(const uint8_t* image, uint64_t size);
uint32_t driver_manager_autoload_from(const char* path);
void driver_manager_register_kernel_exports();
void driver_manager_register_builtin_devices();
void command_drivers();
void command_drvcheck(const char* path);
void command_drvload(const char* path);
void command_drvinfo(const char* path);
void command_drvautoload(const char* path);

#endif
