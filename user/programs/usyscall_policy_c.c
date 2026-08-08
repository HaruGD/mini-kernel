#include <os64/os64.h>

static int denied(long result) {
    return result == OS_ERR_PERMISSION_DENIED;
}

int main(void) {
    uint32_t failures = 0;
    const uintptr_t kernel_address = 0xFFFFFFFF80000000ULL;

    if (!denied(os_msg_v2_recv((OsIpcMessageV2*)kernel_address))) failures |= 1u << 0;
    if (!denied(os_service_find("display", (OsServiceInfo*)kernel_address))) failures |= 1u << 1;
    if (!denied(os_input_poll((OsInputEvent*)kernel_address))) failures |= 1u << 2;
    if (!denied(os_surface_get_info(0, (OsGraphicsSurfaceHandleInfo*)kernel_address))) failures |= 1u << 3;
    if (!denied(os_gfx_clear(0))) failures |= 1u << 4;
    if (!denied(os_run_with_permissions("uhello_c.elf", 0))) failures |= 1u << 5;
    if (os_getpid() <= 0 || os_time_frequency() == 0) failures |= 1u << 6;

    os_printf("[P5S-D] restricted policy failures=%u\n", failures);
    return failures == 0 ? 0 : 1;
}
