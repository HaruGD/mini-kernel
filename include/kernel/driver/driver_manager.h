#ifndef KERNEL_DRIVER_MANAGER_H
#define KERNEL_DRIVER_MANAGER_H

#include <stdint.h>

#define DRIVER_MAX_RECORDS 32
#define DRIVER_MAX_EXPORTS 64
#define DRIVER_MAX_BINDINGS 64
#define DRIVER_MAX_IRQ_HOOKS 32
#define DRIVER_MAX_RESOURCES 128

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
#define DRIVER_STATE_LOADING    5
#define DRIVER_STATE_LINKED     6
#define DRIVER_STATE_QUIESCING  7

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
#define DRIVER_LOAD_SIGNATURE_UNSUPPORTED -12
#define DRIVER_LOAD_UNLOAD_DENIED    -13
#define DRIVER_LOAD_EXIT_FAILED      -14
#define DRIVER_LOAD_MISSING_DEPENDENCY -15
#define DRIVER_LOAD_BIND_DENIED      -16
#define DRIVER_LOAD_IRQ_DENIED       -17
#define DRIVER_LOAD_STATE_DENIED     -18
#define DRIVER_LOAD_STALE_IDENTITY   -19
#define DRIVER_LOAD_RESOURCE_DENIED  -20
#define DRIVER_LOAD_CONTEXT_DENIED   -21
#define DRIVER_LOAD_ALLOCATION_BUDGET -22
#define DRIVER_LOAD_ALLOCATION_DENIED -23
#define DRIVER_LOAD_MMIO_DENIED       -24
#define DRIVER_LOAD_MMIO_RANGE        -25
#define DRIVER_LOAD_MMIO_CACHE        -26
#define DRIVER_LOAD_DMA_DENIED        -27
#define DRIVER_LOAD_DMA_MASK          -28
#define DRIVER_LOAD_DMA_SYNC          -29
#define DRIVER_LOAD_DMA_ISOLATION     -30

#define DRIVER_MAX_LOADED_SECTIONS 16
#define DRIVER_MAX_RESOLVED_IMPORTS 32
#define DRIVER_MAX_DEPENDENCIES 8

#define DRIVER_BIND_KIND_PCI 1

#define DRIVER_RESOURCE_NONE       0
#define DRIVER_RESOURCE_EXPORT     1
#define DRIVER_RESOURCE_PCI_BINDING 2
#define DRIVER_RESOURCE_IRQ_HOOK   3
#define DRIVER_RESOURCE_IMAGE      4
#define DRIVER_RESOURCE_ALLOCATION 5
#define DRIVER_RESOURCE_MMIO       6
#define DRIVER_RESOURCE_DMA        7

#define DRIVER_IDENTITY_INVALID_SLOT 0xFFFFFFFFu

struct DriverIdentity {
    uint32_t slot;
    uint32_t generation;
};

struct DriverDeviceIdentity {
    uint32_t slot;
    uint32_t generation;
};

struct DriverResourceHandle {
    uint32_t slot;
    uint32_t generation;
};

struct DriverVaHandle;

struct DrvManifest;
struct PCIDeviceInfo;

typedef uint64_t (*DriverIrqHandler)(uint64_t irq);

struct DriverLoadedSection {
    uint8_t* base;
    uint64_t size;
    uint32_t page_count;
    uint32_t kind;
    DriverResourceHandle resource;
    uint32_t va_slot;
    uint32_t va_generation;
    char name[16];
};

struct DriverResolvedImport {
    void* address;
    uint32_t required_permission;
    char module[32];
    char name[48];
};

struct DriverResolvedDependency {
    char name[32];
    uint32_t flags;
    uint32_t min_state;
};

struct DriverLoadedImage {
    DriverIdentity owner;
    uint32_t section_count;
    uint32_t import_count;
    uint32_t dependency_count;
    DriverLoadedSection sections[DRIVER_MAX_LOADED_SECTIONS];
    DriverResolvedImport imports[DRIVER_MAX_RESOLVED_IMPORTS];
    DriverResolvedDependency dependencies[DRIVER_MAX_DEPENDENCIES];
    void* entry;
    void* exit;
    void* probe_pci;
};

struct DriverRecord {
    uint8_t active;
    uint8_t state;
    uint16_t kind;
    uint32_t slot;
    uint32_t generation;
    uint32_t permissions;
    char name[32];
    char version[16];
    void* instance;
};

struct DriverExportRecord {
    uint8_t active;
    uint32_t required_permission;
    DriverIdentity owner;
    DriverResourceHandle resource;
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

struct DriverBindingRecord {
    uint8_t active;
    uint8_t kind;
    uint16_t flags;
    uint32_t slot;
    uint32_t generation;
    DriverIdentity owner;
    DriverResourceHandle resource;
    char driver[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

struct DriverIrqHookRecord {
    uint8_t active;
    uint8_t irq;
    uint16_t flags;
    DriverIdentity owner;
    DriverResourceHandle resource;
    uint64_t call_count;
    char driver[32];
    DriverIrqHandler handler;
};

struct DriverResourceRecord {
    uint8_t active;
    uint8_t kind;
    uint16_t flags;
    uint32_t slot;
    uint32_t generation;
    DriverIdentity owner;
    DriverDeviceIdentity device;
    uint64_t value;
    uint64_t size;
    char tag[24];
};

struct DriverResourceStats {
    uint32_t active;
    uint32_t high_water;
    uint32_t by_kind[8];
    uint64_t registrations;
    uint64_t releases;
    uint64_t stale_rejections;
    uint64_t owner_rejections;
    uint64_t state_rejections;
    uint64_t exhaustion_failures;
};

void driver_manager_init();
int driver_manager_register_builtin(const char* name,
                                    const char* version,
                                    uint32_t kind,
                                    uint32_t permissions,
                                    void* instance);
int driver_manager_register_package_manifest(const DrvManifest* manifest, void* instance);
int driver_manager_set_state(const char* name, uint32_t state);
int driver_manager_set_instance(const char* name, void* instance);
int driver_manager_unregister(const char* name);
uint32_t driver_manager_count();
const DriverRecord* driver_manager_get(uint32_t index);
const DriverRecord* driver_manager_find(const char* name);
DriverIdentity driver_identity_invalid();
int driver_identity_is_valid(DriverIdentity identity);
int driver_identity_equal(DriverIdentity left, DriverIdentity right);
DriverIdentity driver_manager_identity_from_name(const char* name);
int driver_manager_identity_is_live(DriverIdentity identity);
int driver_manager_identity_accepts_resources(DriverIdentity identity);
int driver_manager_set_state_identity(DriverIdentity identity, uint32_t state);
const char* driver_state_name(uint32_t state);
const char* driver_kind_name(uint32_t kind);
void driver_manager_set_lifecycle_driver(const char* name);
const char* driver_manager_current_lifecycle_driver();
void driver_manager_clear_last_error();
void driver_manager_set_last_error(int result,
                                   const char* stage,
                                   const char* module,
                                   const char* name,
                                   uint32_t index,
                                   uint64_t detail);
const DriverLoadDiagnostics* driver_manager_last_error();

void driver_resource_init();
DriverResourceHandle driver_resource_invalid();
int driver_resource_handle_is_valid(DriverResourceHandle handle);
int driver_resource_register(DriverIdentity owner,
                             DriverDeviceIdentity device,
                             uint32_t kind,
                             uint32_t flags,
                             uint64_t value,
                             uint64_t size,
                             const char* tag,
                             DriverResourceHandle* out);
int driver_resource_release(DriverIdentity owner,
                            DriverResourceHandle handle,
                            uint32_t expected_kind);
uint32_t driver_resource_release_owner(DriverIdentity owner);
const DriverResourceRecord* driver_resource_resolve(DriverIdentity owner,
                                                    DriverResourceHandle handle,
                                                    uint32_t expected_kind);
void driver_resource_get_stats(DriverResourceStats* out);

int driver_export_register(const char* module,
                           const char* name,
                           void* address,
                           uint32_t required_permission);
void driver_export_unregister_module(const char* module);
void* driver_export_resolve(const char* module, const char* name, uint32_t granted_permissions);
uint32_t driver_export_count();
const DriverExportRecord* driver_export_get(uint32_t index);
void driver_export_init();

int driver_manager_validate_drv_image(const uint8_t* image, uint64_t size);
int driver_manager_load_drv_image(const uint8_t* image, uint64_t size);
int driver_manager_load_from_path(const char* path, int require_automatic);
int driver_manager_unload(const char* name);
int driver_manager_reload_drv_image(const uint8_t* image, uint64_t size);
uint32_t driver_manager_autoload_from(const char* path);
void driver_manager_register_kernel_exports();
void driver_manager_activate_linked_boot();
void driver_manager_activate_linked_kernel();
void driver_manager_activate_linked_runtime();
uint32_t driver_manager_activate_packaged_boot(const struct BootInfo* boot_info);
uint32_t driver_manager_activate_packaged_kernel();
uint32_t driver_manager_activate_packaged_runtime();

int driver_manager_probe_loaded_driver(const char* name, DriverLoadedImage* loaded);
int driver_manager_bind_pci(const char* driver_name, const PCIDeviceInfo* device, uint32_t flags);
void driver_manager_unbind_module(const char* name);
uint32_t driver_manager_binding_count();
const DriverBindingRecord* driver_manager_binding_get(uint32_t index);
void driver_manager_binding_init();
DriverDeviceIdentity driver_device_identity_invalid();
int driver_device_identity_is_valid(DriverDeviceIdentity identity);
int driver_manager_device_identity_is_live(DriverDeviceIdentity identity,
                                           DriverIdentity owner);
int driver_manager_device_identity_resolve(DriverDeviceIdentity identity,
                                           DriverIdentity owner,
                                           DriverBindingRecord* out);
int driver_manager_bound_pci_identity(DriverIdentity owner,
                                      const PCIDeviceInfo* device,
                                      DriverDeviceIdentity* out);

int driver_irq_register_handler(const char* driver_name, uint32_t irq, DriverIrqHandler handler, uint32_t flags);
int driver_irq_unregister_handler(const char* driver_name, uint32_t irq, DriverIrqHandler handler);
void driver_irq_unregister_module(const char* name);
void driver_irq_dispatch(uint32_t irq);
uint32_t driver_irq_hook_count();
const DriverIrqHookRecord* driver_irq_hook_get(uint32_t index);
void driver_irq_init();

void command_drivers();
void command_bindings();
void command_irqhooks();
void command_drvcheck(const char* path);
void command_drvload(const char* path);
void command_drvunload(const char* name);
void command_drvreload(const char* path);
void command_drvinfo(const char* path);
void command_drvautoload(const char* path);
void command_drvlast();

#endif
