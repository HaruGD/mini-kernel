#include "os64/os64.h"
#include "os64/image.h"

#include <limits.h>

#include "image_internal.h"

static uint16_t read_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int32_t read_i32(const uint8_t* value) {
    return (int32_t)read_u32(value);
}

static int dimensions_valid(uint32_t width, uint32_t height) {
    return width != 0 && height != 0 && width <= OS_IMAGE_MAX_DIMENSION &&
           height <= OS_IMAGE_MAX_DIMENSION &&
           (uint64_t)width * height <= OS_IMAGE_MAX_PIXELS &&
           (uint64_t)width * height * 4u <= OS_IMAGE_MAX_BYTES;
}

void os_image_init(OsImage* image) {
    if (image == 0) return;
    os_memset(image, 0, sizeof(*image));
    image->size = sizeof(*image);
    image->abi_version = OS64_IMAGE_ABI_VERSION;
    image->pixel_format = OS_IMAGE_FORMAT_BGRA_PREMULTIPLIED;
}

void os_image_destroy(OsImage* image) {
    if (image == 0) return;
    if (image->size == sizeof(*image) && image->pixels != 0)
        os_free(image->pixels);
    os_image_init(image);
}

long os_image_allocate_internal(uint32_t width,
                                uint32_t height,
                                OsImage* image) {
    if (image == 0 || !dimensions_valid(width, height))
        return OS_ERR_OUT_OF_RANGE;
    uint32_t bytes = width * height * 4u;
    uint32_t* pixels = (uint32_t*)os_malloc(bytes);
    if (pixels == 0) return OS_ERR_OUT_OF_MEMORY;
    os_image_init(image);
    image->width = width;
    image->height = height;
    image->stride_pixels = width;
    image->pixels = pixels;
    image->allocation_bytes = bytes;
    return OS_SUCCESS;
}

uint32_t os_image_premultiply_internal(uint8_t red,
                                       uint8_t green,
                                       uint8_t blue,
                                       uint8_t alpha) {
    uint32_t r = ((uint32_t)red * alpha + 127u) / 255u;
    uint32_t g = ((uint32_t)green * alpha + 127u) / 255u;
    uint32_t b = ((uint32_t)blue * alpha + 127u) / 255u;
    return ((uint32_t)alpha << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t fnv1a(const uint8_t* bytes, uint32_t count) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static long decode_osimg(const uint8_t* bytes,
                         uint32_t byte_count,
                         OsImage* image) {
    if (byte_count < sizeof(OsImageFileHeader)) return OS_ERR_BAD_BUFFER;
    OsImageFileHeader header;
    os_memcpy(&header, bytes, sizeof(header));
    uint64_t expected = (uint64_t)header.stride_pixels * header.height * 4u;
    if (header.magic != OS_IMAGE_OSIMG_MAGIC ||
        header.version != OS_IMAGE_OSIMG_VERSION ||
        header.header_size != sizeof(header) || header.flags != 0 ||
        header.pixel_format != OS_IMAGE_FORMAT_BGRA_PREMULTIPLIED ||
        !dimensions_valid(header.width, header.height) ||
        header.stride_pixels != header.width ||
        expected != header.payload_size || expected > OS_IMAGE_MAX_BYTES ||
        (uint64_t)header.header_size + header.payload_size != byte_count)
        return OS_ERR_BAD_BUFFER;
    const uint8_t* payload = bytes + header.header_size;
    if (fnv1a(payload, header.payload_size) != header.checksum)
        return OS_ERR_IO;
    long result = os_image_allocate_internal(header.width, header.height, image);
    if (result < 0) return result;
    os_memcpy(image->pixels, payload, header.payload_size);
    return OS_SUCCESS;
}

static long decode_bmp(const uint8_t* bytes,
                       uint32_t byte_count,
                       OsImage* image) {
    if (byte_count < 54u || bytes[0] != 'B' || bytes[1] != 'M')
        return OS_ERR_BAD_BUFFER;
    uint32_t pixel_offset = read_u32(bytes + 10);
    uint32_t dib_size = read_u32(bytes + 14);
    int32_t signed_width = read_i32(bytes + 18);
    int32_t signed_height = read_i32(bytes + 22);
    uint16_t planes = read_u16(bytes + 26);
    uint16_t bits = read_u16(bytes + 28);
    uint32_t compression = read_u32(bytes + 30);
    if (dib_size < 40u || signed_width <= 0 || signed_height == 0 ||
        signed_height == INT32_MIN || planes != 1 ||
        (bits != 24u && bits != 32u) || compression != 0)
        return OS_ERR_UNSUPPORTED;
    uint32_t width = (uint32_t)signed_width;
    uint32_t height = signed_height < 0
        ? (uint32_t)(-signed_height) : (uint32_t)signed_height;
    if (!dimensions_valid(width, height)) return OS_ERR_OUT_OF_RANGE;
    uint64_t row_bytes = ((uint64_t)width * bits + 31u) / 32u * 4u;
    uint64_t payload = row_bytes * height;
    if (row_bytes > UINT32_MAX || pixel_offset < 14u + dib_size ||
        (uint64_t)pixel_offset + payload > byte_count)
        return OS_ERR_BAD_BUFFER;
    long result = os_image_allocate_internal(width, height, image);
    if (result < 0) return result;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t source_y = signed_height < 0 ? y : height - 1u - y;
        const uint8_t* row = bytes + pixel_offset + source_y * row_bytes;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* pixel = row + x * (bits / 8u);
            uint8_t alpha = bits == 32u ? pixel[3] : 255u;
            image->pixels[y * width + x] = os_image_premultiply_internal(
                pixel[2], pixel[1], pixel[0], alpha);
        }
    }
    return OS_SUCCESS;
}

long os_image_decode(const void* data,
                     uint32_t byte_count,
                     uint32_t format_hint,
                     OsImage* image) {
    if (data == 0 || byte_count == 0 || image == 0 ||
        format_hint > OS_IMAGE_DECODE_PNG) return OS_ERR_INVALID_ARGUMENT;
    os_image_destroy(image);
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t format = format_hint;
    if (format == OS_IMAGE_DECODE_AUTO) {
        if (byte_count >= 4u && read_u32(bytes) == OS_IMAGE_OSIMG_MAGIC)
            format = OS_IMAGE_DECODE_OSIMG;
        else if (byte_count >= 2u && bytes[0] == 'B' && bytes[1] == 'M')
            format = OS_IMAGE_DECODE_BMP;
        else if (byte_count >= 8u && bytes[0] == 0x89u && bytes[1] == 'P')
            format = OS_IMAGE_DECODE_PNG;
        else return OS_ERR_UNSUPPORTED;
    }
    long result = format == OS_IMAGE_DECODE_OSIMG
        ? decode_osimg(bytes, byte_count, image)
        : format == OS_IMAGE_DECODE_BMP
            ? decode_bmp(bytes, byte_count, image)
            : os_image_decode_png_internal(bytes, byte_count, image);
    if (result < 0) os_image_destroy(image);
    return result;
}

long os_image_load(const char* path, OsImage* image) {
    if (path == 0 || image == 0) return OS_ERR_INVALID_ARGUMENT;
    uint32_t size = 0;
    void* bytes = os_read_file_alloc(path, &size);
    if (bytes == 0) return OS_ERR_NOT_FOUND;
    long result = os_image_decode(bytes, size, OS_IMAGE_DECODE_AUTO, image);
    os_free(bytes);
    return result;
}

static int image_valid(const OsImage* image) {
    return image != 0 && image->size == sizeof(*image) &&
           image->abi_version == OS64_IMAGE_ABI_VERSION && image->pixels != 0 &&
           image->pixel_format == OS_IMAGE_FORMAT_BGRA_PREMULTIPLIED &&
           dimensions_valid(image->width, image->height) &&
           image->stride_pixels >= image->width &&
           (uint64_t)image->stride_pixels * image->height * 4u <=
               image->allocation_bytes;
}

static uint32_t blend(uint32_t source, uint32_t destination) {
    uint32_t alpha = source >> 24;
    uint32_t inverse = 255u - alpha;
    uint32_t sr = (source >> 16) & 0xFFu;
    uint32_t sg = (source >> 8) & 0xFFu;
    uint32_t sb = source & 0xFFu;
    uint32_t dr = (destination >> 16) & 0xFFu;
    uint32_t dg = (destination >> 8) & 0xFFu;
    uint32_t db = destination & 0xFFu;
    uint32_t r = sr + (dr * inverse + 127u) / 255u;
    uint32_t g = sg + (dg * inverse + 127u) / 255u;
    uint32_t b = sb + (db * inverse + 127u) / 255u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

long os_surface_canvas_draw_image(OsSurfaceCanvas* canvas,
                                  const OsImage* image,
                                  OsRect source,
                                  OsRect destination,
                                  uint32_t scale_mode,
                                  OsRect* damage_out) {
    if (canvas == 0 || canvas->pixels == 0 || !image_valid(image) ||
        scale_mode != OS_IMAGE_SCALE_NEAREST || source.width <= 0 ||
        source.height <= 0 || destination.width <= 0 || destination.height <= 0 ||
        source.x < 0 || source.y < 0 ||
        (int64_t)source.x + source.width > image->width ||
        (int64_t)source.y + source.height > image->height ||
        canvas->stride_pixels < canvas->width)
        return OS_ERR_INVALID_ARGUMENT;
    int64_t left = destination.x < 0 ? 0 : destination.x;
    int64_t top = destination.y < 0 ? 0 : destination.y;
    int64_t right = (int64_t)destination.x + destination.width;
    int64_t bottom = (int64_t)destination.y + destination.height;
    if (right > canvas->width) right = canvas->width;
    if (bottom > canvas->height) bottom = canvas->height;
    if (right <= left || bottom <= top) {
        if (damage_out != 0) *damage_out = (OsRect){0, 0, 0, 0};
        return OS_SUCCESS;
    }
    for (int64_t y = top; y < bottom; y++) {
        uint32_t sy = (uint32_t)source.y +
            (uint32_t)(((uint64_t)(y - destination.y) * source.height) /
                       destination.height);
        for (int64_t x = left; x < right; x++) {
            uint32_t sx = (uint32_t)source.x +
                (uint32_t)(((uint64_t)(x - destination.x) * source.width) /
                           destination.width);
            uint32_t src = image->pixels[sy * image->stride_pixels + sx];
            uint32_t* dst = &canvas->pixels[(uint32_t)y * canvas->stride_pixels +
                                            (uint32_t)x];
            *dst = blend(src, *dst);
        }
    }
    if (damage_out != 0)
        *damage_out = (OsRect){(int32_t)left, (int32_t)top,
                               (int32_t)(right - left),
                               (int32_t)(bottom - top)};
    return OS_SUCCESS;
}
