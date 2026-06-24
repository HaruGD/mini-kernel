#include <os64/os64.h>

static const uint32_t SPRITE_PIXELS[8 * 8] = {
    0x00FF00FF, 0x00FF00FF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FF00FF, 0x00FF00FF,
    0x00FF00FF, 0x00FFFFFF, 0x0000C853, 0x0000C853, 0x0000C853, 0x0000C853, 0x00FFFFFF, 0x00FF00FF,
    0x00FFFFFF, 0x0000C853, 0x0000C853, 0x00000000, 0x00000000, 0x0000C853, 0x0000C853, 0x00FFFFFF,
    0x00FFFFFF, 0x0000C853, 0x0000C853, 0x0000C853, 0x0000C853, 0x0000C853, 0x0000C853, 0x00FFFFFF,
    0x00FFFFFF, 0x0000C853, 0x00000000, 0x0000C853, 0x0000C853, 0x00000000, 0x0000C853, 0x00FFFFFF,
    0x00FFFFFF, 0x0000C853, 0x0000C853, 0x00000000, 0x00000000, 0x0000C853, 0x0000C853, 0x00FFFFFF,
    0x00FF00FF, 0x00FFFFFF, 0x0000C853, 0x0000C853, 0x0000C853, 0x0000C853, 0x00FFFFFF, 0x00FF00FF,
    0x00FF00FF, 0x00FF00FF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FF00FF, 0x00FF00FF,
};

int main(void) {
    OsGraphicsInfo info;
    if (os_gfx_get_info(&info) != OS_SUCCESS) {
        os_puts("ugfxdemo: graphics not ready");
        return 1;
    }

    OsBitmap sprite;
    OsRect sprite_rect;
    sprite.pixels = SPRITE_PIXELS;
    sprite.width = 8;
    sprite.height = 8;
    sprite.stride_pixels = 8;
    sprite.pixel_format = OS64_PIXEL_FORMAT_RGB;
    sprite_rect.x = 0;
    sprite_rect.y = 0;
    sprite_rect.width = 8;
    sprite_rect.height = 8;

    os_gfx_clear(OS_RGB(8, 18, 32));
    os_gfx_fill_rect(20, 20, 180, 72, OS_RGB(37, 94, 232));
    os_gfx_fill_rect(32, 34, 156, 44, OS_RGB(0, 200, 83));
    os_gfx_draw_line(20, 20, 199, 91, OS_RGB(255, 255, 255));
    os_gfx_draw_line(199, 20, 20, 91, OS_RGB(255, 248, 225));
    os_gfx_draw_line(16, 110, 260, 110, OS_RGB(249, 168, 37));
    os_gfx_draw_line(16, 110, 16, 170, OS_RGB(249, 168, 37));
    os_gfx_draw_text(24, 104, "OS64 2D DEMO", OS_RGB(255, 255, 255), OS_RGB(8, 18, 32), OS_GFX_TEXT_TRANSPARENT_BG);

    os_gfx_blit(&sprite, &sprite_rect, 42, 128);
    os_gfx_blit_keyed(&sprite, &sprite_rect, 62, 128, OS_RGB(255, 0, 255));
    os_gfx_draw_text(84, 130, "BLIT + KEY", OS_RGB(255, 255, 255), OS_RGB(8, 18, 32), OS_GFX_TEXT_TRANSPARENT_BG);

    os_printf("ugfxdemo: drew %ux%u surface demo\n", info.width, info.height);
    return 0;
}
