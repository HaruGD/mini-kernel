#!/usr/bin/env python3
import subprocess, tempfile, textwrap
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <stdint.h>
#include "kernel/driver/driver_dma.h"
#include "kernel/driver/drv_format.h"
#include "kernel/pci.h"
#include "kernel/fault_injection.h"
static int failures;
#define check(v) do{if(!(v))failures++;}while(0)
static DrvManifest manifest(const char*n){DrvManifest v={};for(uint32_t i=0;n[i]&&i+1<sizeof(v.name);i++)v.name[i]=n[i];v.version[0]='1';v.entry_symbol[0]='e';v.boot_modes=DRV_BOOT_NORMAL;v.permissions=DRV_PERMISSION_PCI|DRV_PERMISSION_DMA;return v;}
int main(){
 driver_manager_init();driver_allocation_init();driver_dma_init();
 DrvManifest am=manifest("alpha");check(driver_manager_register_package_manifest(&am,0)==0);
 DriverIdentity a=driver_manager_identity_from_name("alpha");check(driver_manager_set_state_identity(a,DRIVER_STATE_LOADING)==0);
 const PCIDeviceInfo*pci=pci_get_device(0);check(driver_manager_bind_pci("alpha",pci,0)==0);DriverDeviceIdentity dev;check(driver_manager_bound_pci_identity(a,pci,&dev));
 DriverDmaDomainHandle domain;check(driver_dma_prepare_device(a,dev,DRIVER_DMA_POLICY_TRUSTED_DIRECT,&domain)==0);check(driver_dma_set_mask(a,dev,32)==0);
 DriverAllocationResult one,two;check(driver_allocation_create(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,5000,4096,DRIVER_ALLOC_PAGES|DRIVER_ALLOC_ZERO,"one",&one)==0);check(driver_allocation_create(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,3000,16,DRIVER_ALLOC_ZERO,"two",&two)==0);
 DriverDmaMapping from;check(driver_dma_map_buffer(a,dev,one.handle,0,5000,DRIVER_DMA_FROM_DEVICE,&from)==0);check(from.segment_count>=2);
 check(driver_allocation_release(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,one.handle)==DRIVER_LOAD_ALLOCATION_DENIED);check(driver_dma_unmap(a,from.handle)==DRIVER_LOAD_DMA_SYNC);check(driver_dma_sync_for_cpu(a,from.handle)==0);check(driver_dma_sync_for_cpu(a,from.handle)==DRIVER_LOAD_DMA_SYNC);check(driver_dma_sync_for_device(a,from.handle)==0);check(driver_dma_sync_for_cpu(a,from.handle)==0);check(driver_dma_unmap(a,from.handle)==0);check(driver_dma_unmap(a,from.handle)==DRIVER_LOAD_DMA_DENIED);
 DriverDmaMapping to;check(driver_dma_map_buffer(a,dev,two.handle,8,1024,DRIVER_DMA_TO_DEVICE,&to)==0);check(driver_dma_sync_for_cpu(a,to.handle)==DRIVER_LOAD_DMA_SYNC);check(driver_dma_unmap(a,to.handle)==0);
 DriverDmaSource sources[2]={{one.handle,100,2000},{two.handle,32,2000}};DriverDmaMapping sg;check(driver_dma_map_sg(a,dev,sources,2,DRIVER_DMA_BIDIRECTIONAL,&sg)==0);check(sg.segment_count>=2);check(driver_dma_sync_for_cpu(a,sg.handle)==0);check(driver_dma_unmap(a,sg.handle)==0);
 check(driver_dma_set_mask(a,dev,24)==0);DriverDmaMapping bounced;kernel_fault_injection_reset();check(kernel_fault_injection_arm(KERNEL_FAULT_POINT_DRIVER_DMA_BOUNCE,0));check(driver_dma_map_buffer(a,dev,one.handle,0,4096,DRIVER_DMA_TO_DEVICE,&bounced)==DRIVER_LOAD_OUT_OF_MEMORY);check(driver_dma_map_buffer(a,dev,one.handle,0,4096,DRIVER_DMA_TO_DEVICE,&bounced)==0);check(bounced.segment_count==1);check(driver_dma_unmap(a,bounced.handle)==0);check(driver_dma_set_mask(a,dev,32)==0);
 DriverDmaMapping bad;check(driver_dma_map_buffer(a,dev,one.handle,4999,2,DRIVER_DMA_TO_DEVICE,&bad)==DRIVER_LOAD_ALLOCATION_DENIED);check(driver_dma_map_buffer(a,dev,one.handle,0,1,99,&bad)==DRIVER_LOAD_BAD_HEADER);
 DriverAllocationResult fragmented;check(driver_allocation_create(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,33*4096,4096,DRIVER_ALLOC_PAGES,"fragmented",&fragmented)==0);check(driver_dma_map_buffer(a,dev,fragmented.handle,0,33*4096,DRIVER_DMA_TO_DEVICE,&bad)==DRIVER_LOAD_NO_SLOT);check(driver_allocation_release(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,fragmented.handle)==0);
 check(driver_allocation_release(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,one.handle)==0);check(driver_allocation_release(a,DRIVER_CONTEXT_THREAD_SLEEPABLE,two.handle)==0);check(driver_dma_release_owner(a)==0);check(driver_dma_owner_count(a)==0);
 DriverDmaStats ds;driver_dma_get_stats(&ds);check(ds.streaming_mappings==0&&ds.pinned_sources==0&&ds.bounce_mappings==0&&ds.streaming_maps==4&&ds.streaming_unmaps==4);check(ds.sync_rejections>=3&&ds.segment_overflow==1);DriverAllocationStats as;driver_allocation_get_stats(&as);check(as.active==0&&as.pinned_ranges==0&&as.pinned_free_rejections==1);return failures?1:0;
}
'''
STUBS=r'''
#include <stdint.h>
#include <stdlib.h>
#include "kernel/pci.h"
#include "kernel/driver/driver_alloc.h"
static PCIDeviceInfo device={0x1af4,0x1001,0,0,0,3,0,0,0,0,2,0,0,11,1,1,{0,0,0},{0x10000000,0,0,0,0,0}};
int strcmp64(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return(unsigned char)*a-(unsigned char)*b;}void copy_string64(char*out,uint32_t c,const char*t){uint32_t i=0;if(!out||!c)return;if(t)for(;t[i]&&i+1<c;i++)out[i]=t[i];out[i]=0;}
uint32_t pci_get_device_count(){return 1;}const PCIDeviceInfo*pci_get_device(uint32_t i){return i?0:&device;}int pci_enable_bus_mastering(const PCIDeviceInfo*){return 1;}int pci_disable_bus_mastering(const PCIDeviceInfo*){return 1;}
void driver_image_va_init(){}void driver_irq_init(){}void driver_export_init(){}
extern "C" void*kmalloc(size_t n){return malloc(n);}extern "C" void kfree(void*p){free(p);}
'''
def main():
 with tempfile.TemporaryDirectory(prefix="os64_dma_stream_") as t:
  p=Path(t);(p/"test.cpp").write_text(textwrap.dedent(SOURCE));(p/"stubs.cpp").write_text(textwrap.dedent(STUBS));binary=p/"test"
  subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-DOS64_HOST_TEST","-DOS64_DRIVER_HOST_TEST","-I",str(ROOT/"include"),str(ROOT/"kernel/sync/spinlock.cpp"),str(ROOT/"kernel/driver/driver_manager.cpp"),str(ROOT/"kernel/driver/driver_resource.cpp"),str(ROOT/"kernel/driver/driver_binding.cpp"),str(ROOT/"kernel/driver/driver_alloc.cpp"),str(ROOT/"kernel/driver/driver_dma.cpp"),str(ROOT/"kernel/debug/fault_injection.cpp"),str(p/"test.cpp"),str(p/"stubs.cpp"),"-o",str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
 print("streaming DMA, SG, pin, sync, and direct-domain test OK");return 0
if __name__=="__main__":raise SystemExit(main())
