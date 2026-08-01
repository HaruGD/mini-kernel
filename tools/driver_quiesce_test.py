#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <stdint.h>
#include <thread>
#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"

static int failures;
int events[16];
uint32_t event_count;
#define check(v) do { if (!(v)) failures++; } while (0)

static DrvManifest manifest(const char* name) {
    DrvManifest value = {};
    for (uint32_t i = 0; name[i] && i + 1 < sizeof(value.name); i++)
        value.name[i] = name[i];
    value.version[0] = '1';
    value.entry_symbol[0] = 'e';
    value.boot_modes = DRV_BOOT_NORMAL;
    return value;
}

static void ready(const char* name, DriverLoadedImage* loaded,
                  DriverIdentity* identity) {
    DrvManifest value = manifest(name);
    check(driver_manager_register_package_manifest(&value, loaded) == 0);
    *identity = driver_manager_identity_from_name(name);
    loaded->owner = *identity;
    check(driver_manager_set_state_identity(*identity, DRIVER_STATE_LOADING) == 0);
    check(driver_manager_set_state_identity(*identity, DRIVER_STATE_LINKED) == 0);
    check(driver_manager_set_state_identity(*identity, DRIVER_STATE_READY) == 0);
}

int main() {
    driver_manager_init();
    DriverLoadedImage* alpha_image = new DriverLoadedImage{};
    DriverIdentity alpha;
    ready("alpha", alpha_image, &alpha);

    DriverActivityToken pins[4] = {};
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_CALL, &pins[0]) == 0);
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_IRQ, &pins[1]) == 0);
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_WORK, &pins[2]) == 0);
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_DMA, &pins[3]) == 0);
    check(driver_manager_begin_quiesce(alpha) == 0);
    DriverActivityToken denied = {};
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_CALL, &denied) ==
          DRIVER_LOAD_STATE_DENIED);
    check(driver_manager_wait_quiesced(alpha, 32) ==
          DRIVER_LOAD_QUIESCE_TIMEOUT);
    for (uint32_t i = 0; i < 4; i++) driver_manager_activity_unpin(&pins[i]);
    check(driver_manager_wait_quiesced(alpha, 32) == 0);
    DriverQuiesceStats stats;
    driver_manager_quiesce_get_stats(&stats);
    check(stats.in_flight == 0 && stats.calls == 0 && stats.irqs == 0);
    check(stats.work == 0 && stats.dma == 0);
    check(stats.pins == 4 && stats.unpins == 4);
    check(stats.rejected_entries >= 1 && stats.timeouts == 1 &&
          stats.quarantines == 1);

    check(driver_manager_unregister("alpha") == 0);
    DriverLoadedImage* reused_image = new DriverLoadedImage{};
    DriverIdentity reused;
    ready("alpha", reused_image, &reused);
    check(reused.generation != alpha.generation);
    check(driver_manager_activity_pin(alpha, DRIVER_ACTIVITY_CALL, &denied) ==
          DRIVER_LOAD_STATE_DENIED);
    check(driver_manager_unload("alpha") == 0);
    check(driver_manager_find("alpha") == 0);
    check(event_count >= 6);
    check(events[0] == 1); /* IRQ admission closes first. */
    check(events[1] == 2); /* Bus mastering stops before teardown. */
    check(events[2] == 3); /* DMA is reclaimed before MMIO/alloc/image. */
    check(events[3] == 4 && events[4] == 5 && events[5] == 6);

    DriverLoadedImage* churn_image = new DriverLoadedImage{};
    DriverIdentity churn;
    ready("churn", churn_image, &churn);
    volatile uint32_t stop = 0;
    volatile uint32_t entered = 0;
    std::thread workers[4];
    for (uint32_t t = 0; t < 4; t++) workers[t] = std::thread([&]() {
        while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
            DriverActivityToken token = {};
            if (driver_manager_activity_pin(churn, DRIVER_ACTIVITY_WORK,
                                            &token) == 0) {
                __atomic_add_fetch(&entered, 1u, __ATOMIC_RELAXED);
                driver_manager_activity_unpin(&token);
            }
        }
    });
    while (__atomic_load_n(&entered, __ATOMIC_ACQUIRE) < 1000) {}
    check(driver_manager_begin_quiesce(churn) == 0);
    __atomic_store_n(&stop, 1u, __ATOMIC_RELEASE);
    for (auto& worker : workers) worker.join();
    check(driver_manager_wait_quiesced(churn, 1000000) == 0);
    check(driver_manager_activity_pin(churn, DRIVER_ACTIVITY_WORK, &denied) ==
          DRIVER_LOAD_STATE_DENIED);
    delete churn_image;
    check(driver_manager_unregister("churn") == 0);
    return failures ? 1 : 0;
}
'''

STUBS = r'''
#include <stdint.h>
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/driver_dma.h"
#include "kernel/driver/driver_mmio.h"
#include "kernel/driver/driver_va.h"

extern int events[16];
extern uint32_t event_count;
int strcmp64(const char* a,const char* b){while(*a&&*a==*b){a++;b++;}return(unsigned char)*a-(unsigned char)*b;}
void copy_string64(char* out,uint32_t cap,const char* text){uint32_t i=0;if(!out||!cap)return;if(text)for(;text[i]&&i+1<cap;i++)out[i]=text[i];out[i]=0;}
void driver_image_va_init(){}
void driver_manager_binding_init(){}
DriverDeviceIdentity driver_device_identity_invalid(){return {DRIVER_IDENTITY_INVALID_SLOT,0};}
void driver_irq_init(){}
void driver_export_init(){}
void driver_irq_unregister_module(const char*){events[event_count++]=1;}
uint32_t driver_dma_quiesce_owner(DriverIdentity){events[event_count++]=2;return 0;}
uint32_t driver_dma_release_owner(DriverIdentity){events[event_count++]=3;return 0;}
uint32_t driver_dma_owner_count(DriverIdentity){return 0;}
uint32_t driver_mmio_release_owner(DriverIdentity){events[event_count++]=4;return 0;}
uint32_t driver_mmio_owner_count(DriverIdentity){return 0;}
void driver_manager_unbind_module(const char*){events[event_count++]=5;}
uint32_t driver_allocation_release_owner(DriverIdentity){events[event_count++]=6;return 0;}
uint32_t driver_allocation_owner_count(DriverIdentity){return 0;}
void driver_export_unregister_module(const char*){}
int driver_execution_enter_quiesce(DriverIdentity,uint32_t,DriverExecutionToken* token){if(token)token->active=1;return 0;}
void driver_execution_leave(DriverExecutionToken* token){if(token)token->active=0;}
extern "C" uint32_t vm_unmap_free_range_tlb_safe(uint64_t,uint32_t){return 0;}
int driver_image_va_quarantine(DriverIdentity,DriverVaHandle){return 0;}
int driver_image_va_release(DriverIdentity,DriverVaHandle){return 0;}
int driver_manager_validate_drv_image(const uint8_t*,uint64_t){return DRIVER_LOAD_BAD_HEADER;}
int driver_manager_load_drv_image(const uint8_t*,uint64_t){return DRIVER_LOAD_BAD_HEADER;}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_driver_quiesce_") as temp:
        path = Path(temp)
        (path / "test.cpp").write_text(textwrap.dedent(SOURCE))
        (path / "stubs.cpp").write_text(textwrap.dedent(STUBS))
        binary = path / "test"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/driver/driver_manager.cpp"),
            str(ROOT / "kernel/driver/driver_resource.cpp"),
            str(ROOT / "kernel/driver/driver_unload.cpp"),
            str(path / "test.cpp"), str(path / "stubs.cpp"),
            "-pthread", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("driver quiescent unload, timeout quarantine, and SMP pin test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
