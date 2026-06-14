#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "drivers/ata.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "fs/fat32.h"

extern ATADriver ata;
extern KeyboardDriver keyboard;
extern PIT pit;
extern FAT32Driver fat32;
extern FAT32Driver* root_fat32;

static void register_ready_builtin(const char* name,
                                   uint32_t kind,
                                   uint32_t permissions,
                                   void* instance) {
    if (driver_manager_register_builtin(name, "builtin", kind, permissions, instance) == DRIVER_LOAD_OK) {
        driver_manager_set_state(name, DRIVER_STATE_READY);
    }
}

void driver_manager_register_builtin_devices() {
    register_ready_builtin("ata0",
                           DRIVER_KIND_BLOCK,
                           DRV_PERMISSION_BLOCK,
                           &ata);
    register_ready_builtin("fat32",
                           DRIVER_KIND_FS,
                           DRV_PERMISSION_BLOCK | DRV_PERMISSION_VFS,
                           root_fat32 != 0 ? root_fat32 : &fat32);
    register_ready_builtin("keyboard",
                           DRIVER_KIND_INPUT,
                           DRV_PERMISSION_INPUT | DRV_PERMISSION_INTERRUPT,
                           &keyboard);
    register_ready_builtin("pit",
                           DRIVER_KIND_TIMER,
                           DRV_PERMISSION_TIMER | DRV_PERMISSION_INTERRUPT,
                           &pit);
}
