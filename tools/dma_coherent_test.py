#!/usr/bin/env python3
import subprocess, tempfile, textwrap
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <stdint.h>
#include "kernel/driver/driver_dma.h"
#include "kernel/driver/drv_format.h"
#include "kernel/pci.h"
static int failures;
#define check(v) do { if (!(v)) failures++; } while (0)
static DrvManifest manifest(const char* name) {
    DrvManifest v = {}; for (uint32_t i=0; name[i] && i+1<sizeof(v.name); i++) v.name[i]=name[i];
    v.version[0]='1'; v.entry_symbol[0]='e'; v.boot_modes=DRV_BOOT_NORMAL;
    v.permissions=DRV_PERMISSION_PCI|DRV_PERMISSION_DMA; return v;
}
int main() {
    driver_manager_init(); driver_dma_init();
    DrvManifest am=manifest("alpha"), bm=manifest("beta");
    check(driver_manager_register_package_manifest(&am,0)==0);
    check(driver_manager_register_package_manifest(&bm,0)==0);
    DriverIdentity a=driver_manager_identity_from_name("alpha");
    DriverIdentity b=driver_manager_identity_from_name("beta");
    check(driver_manager_set_state_identity(a,DRIVER_STATE_LOADING)==0);
    check(driver_manager_set_state_identity(b,DRIVER_STATE_LOADING)==0);
    const PCIDeviceInfo* pci=pci_get_device(0);
    check(driver_manager_bind_pci("alpha",pci,0)==0);
    DriverDeviceIdentity dev; check(driver_manager_bound_pci_identity(a,pci,&dev));
    check(driver_dma_enable_bus_mastering(a,dev)==DRIVER_LOAD_DMA_DENIED);
    DriverDmaDomainHandle domain;
    check(driver_dma_prepare_device(a,dev,DRIVER_DMA_POLICY_REQUIRE_ISOLATION,&domain)==DRIVER_LOAD_DMA_ISOLATION);
    check(driver_dma_prepare_device(a,dev,DRIVER_DMA_POLICY_TRUSTED_DIRECT,&domain)==0);
    check(driver_dma_set_mask(a,dev,16)==DRIVER_LOAD_DMA_MASK);
    check(driver_dma_enable_bus_mastering(a,dev)==DRIVER_LOAD_DMA_DENIED);
    check(driver_dma_set_mask(a,dev,32)==0);
    DriverDmaBuffer buffer;
    check(driver_dma_alloc_coherent(a,dev,5000,8192,65536,&buffer)==0);
    check(buffer.cpu_address && buffer.size==5000 && buffer.page_count==2);
    check((buffer.dma_address.value & 8191u)==0);
    check(buffer.dma_address.value/65536u==(buffer.dma_address.value+8191u)/65536u);
    for (uint32_t i=0;i<8192;i++) check(((uint8_t*)buffer.cpu_address)[i]==0);
    check(driver_dma_free_coherent(b,buffer.handle)==DRIVER_LOAD_DMA_DENIED);
    check(driver_dma_set_mask(a,dev,64)==DRIVER_LOAD_DMA_DENIED);
    check(driver_dma_enable_bus_mastering(a,dev)==0);
    check(driver_dma_free_coherent(a,buffer.handle)==0);
    check(driver_dma_free_coherent(a,buffer.handle)==DRIVER_LOAD_DMA_DENIED);
    DriverDmaBuffer whole, over;
    check(driver_dma_alloc_coherent(a,dev,DRIVER_DMA_OWNER_BUDGET,4096,0,&whole)==0);
    check(driver_dma_alloc_coherent(a,dev,1,1,0,&over)==DRIVER_LOAD_ALLOCATION_BUDGET);
    check(driver_dma_free_coherent(a,whole.handle)==0);
    DriverDmaBuffer leaked;
    check(driver_dma_alloc_coherent(a,dev,4096,4096,0,&leaked)==0);
    check(driver_dma_release_owner(a)==1);
    check(driver_dma_owner_count(a)==0);
    DriverDmaStats stats; driver_dma_get_stats(&stats);
    check(stats.domains==0 && stats.coherent_buffers==0 && stats.coherent_bytes==0);
    check(stats.isolation_rejections==1 && stats.bus_master_rejections==2);
    check(stats.owner_rejections==1 && stats.stale_rejections==1);
    DriverResourceStats resources; driver_resource_get_stats(&resources);
    check(resources.by_kind[DRIVER_RESOURCE_DMA]==0);
    return failures ? 1 : 0;
}
'''
STUBS = r'''
#include <stdint.h>
#include "kernel/pci.h"
#include "kernel/driver/driver_alloc.h"
static PCIDeviceInfo device={0x1af4,0x1001,0,0,0,3,0,0,0,0,2,0,0,11,1,1,{0,0,0},{0x10000000,0,0,0,0,0}};
int strcmp64(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return(unsigned char)*a-(unsigned char)*b;}
void copy_string64(char*out,uint32_t cap,const char*t){uint32_t i=0;if(!out||!cap)return;if(t)for(;t[i]&&i+1<cap;i++)out[i]=t[i];out[i]=0;}
uint32_t pci_get_device_count(){return 1;} const PCIDeviceInfo* pci_get_device(uint32_t i){return i?0:&device;}
int pci_enable_bus_mastering(const PCIDeviceInfo*){return 1;} int pci_disable_bus_mastering(const PCIDeviceInfo*){return 1;}
void driver_image_va_init(){} void driver_irq_init(){} void driver_export_init(){}
int driver_execution_current(DriverExecutionContext*){return 0;}
int driver_execution_enter(DriverIdentity,uint32_t,DriverExecutionToken*t){if(t)t->active=1;return 0;}
void driver_execution_leave(DriverExecutionToken*){}
int driver_allocation_pin(DriverIdentity,DriverAllocationHandle,uint64_t,uint64_t,DriverPinnedAllocation*){return -1;}
int driver_allocation_unpin(DriverIdentity,DriverAllocationHandle){return -1;}
'''
def main():
    with tempfile.TemporaryDirectory(prefix="os64_dma_") as t:
        p=Path(t); (p/"test.cpp").write_text(textwrap.dedent(SOURCE)); (p/"stubs.cpp").write_text(textwrap.dedent(STUBS))
        cmd=["g++","-std=c++17","-Wall","-Wextra","-Werror","-DOS64_HOST_TEST","-DOS64_DRIVER_HOST_TEST","-I",str(ROOT/"include"),str(ROOT/"kernel/sync/spinlock.cpp"),str(ROOT/"kernel/driver/driver_manager.cpp"),str(ROOT/"kernel/driver/driver_resource.cpp"),str(ROOT/"kernel/driver/driver_binding.cpp"),str(ROOT/"kernel/driver/driver_dma.cpp"),str(p/"test.cpp"),str(p/"stubs.cpp"),"-o",str(p/"test")]
        subprocess.run(cmd,check=True); subprocess.run([str(p/"test")],check=True)
    print("coherent DMA address, mask, budget, and domain test OK"); return 0
if __name__=="__main__": raise SystemExit(main())
