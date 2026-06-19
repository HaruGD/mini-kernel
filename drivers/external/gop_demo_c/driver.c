#include "os64_driver.h"

static const char entry_message[] OS64_EXPORT = "gop_demo_c.drv driver_entry()";
static const char ready_message[] OS64_EXPORT = "gop_demo_c.drv GOP draw OK";
static const char missing_message[] OS64_EXPORT = "gop_demo_c.drv GOP not ready";

static os64_u32 min_u32(os64_u32 a, os64_u32 b) {
    return a < b ? a : b;
}

os64_u64 driver_entry(void) {
    os64_klog(entry_message);

    const os64_gop_info* info = os64_gop_get_info();
    if (info == 0 || info->width == 0 || info->height == 0) {
        os64_klog(missing_message);
        return 0;
    }

    os64_u32 bar_height = min_u32(18, info->height);
    os64_u32 y = info->height - bar_height;
    os64_u32 third = info->width / 3;
    if (third == 0) {
        third = 1;
    }

    os64_gop_fill_rect(0, y, third, bar_height, 0x00255EE8);
    os64_gop_fill_rect(third, y, third, bar_height, 0x0000C853);
    os64_gop_fill_rect(third * 2, y, info->width - (third * 2), bar_height, 0x00F9A825);

    os64_u32 mark = min_u32(48, min_u32(info->width, info->height));
    for (os64_u32 i = 0; i < mark; i++) {
        os64_gop_putpixel(i, i, 0x00FFFFFF);
        os64_gop_putpixel(mark - 1 - i, i, 0x00FFFFFF);
    }

    os64_klog(ready_message);
    return 0;
}
