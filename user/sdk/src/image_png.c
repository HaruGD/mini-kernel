#include "os64/os64.h"

#include "image_internal.h"

#define PNG_MAX_CHUNKS 1024u
#define PNG_MAX_COMPRESSED (32u * 1024u * 1024u)
#define PNG_MAX_WORK (300u * 1024u * 1024u)

typedef struct BitReader {
    const uint8_t* bytes;
    uint32_t size;
    uint32_t byte_at;
    uint32_t bits;
    uint32_t bit_count;
    uint32_t work;
} BitReader;

typedef struct Huffman {
    uint16_t count[16];
    uint16_t symbol[288];
} Huffman;

static uint32_t be32(const uint8_t* value) {
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | value[3];
}

static uint32_t crc_table_value(uint32_t index) {
    uint32_t value = index;
    for (uint32_t i = 0; i < 8u; i++)
        value = (value & 1u) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
    return value;
}

static uint32_t png_crc(const uint8_t* bytes, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++)
        crc = crc_table_value((crc ^ bytes[i]) & 0xFFu) ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static int get_bits(BitReader* reader, uint32_t count, uint32_t* value) {
    if (reader == 0 || value == 0 || count > 16u ||
        reader->work++ >= PNG_MAX_WORK) return 0;
    while (reader->bit_count < count) {
        if (reader->byte_at >= reader->size) return 0;
        reader->bits |= (uint32_t)reader->bytes[reader->byte_at++] <<
                        reader->bit_count;
        reader->bit_count += 8u;
    }
    *value = reader->bits & ((1u << count) - 1u);
    reader->bits >>= count;
    reader->bit_count -= count;
    return 1;
}

static int build_huffman(Huffman* tree,
                         const uint8_t* lengths,
                         uint32_t count) {
    uint16_t offsets[16];
    os_memset(tree, 0, sizeof(*tree));
    if (count == 0 || count > 288u) return 0;
    for (uint32_t symbol = 0; symbol < count; symbol++) {
        if (lengths[symbol] > 15u) return 0;
        tree->count[lengths[symbol]]++;
    }
    if (tree->count[0] == count) return 0;
    int32_t left = 1;
    for (uint32_t length = 1; length <= 15u; length++) {
        left = (left << 1) - tree->count[length];
        if (left < 0) return 0;
    }
    offsets[1] = 0;
    for (uint32_t length = 1; length < 15u; length++)
        offsets[length + 1u] = offsets[length] + tree->count[length];
    for (uint32_t symbol = 0; symbol < count; symbol++)
        if (lengths[symbol] != 0)
            tree->symbol[offsets[lengths[symbol]]++] = (uint16_t)symbol;
    return 1;
}

static int decode_symbol(BitReader* reader,
                         const Huffman* tree,
                         uint32_t* symbol) {
    uint32_t code = 0;
    uint32_t first = 0;
    uint32_t index = 0;
    for (uint32_t length = 1; length <= 15u; length++) {
        uint32_t bit;
        if (!get_bits(reader, 1, &bit)) return 0;
        code |= bit;
        uint32_t count = tree->count[length];
        if (code >= first && code - first < count) {
            *symbol = tree->symbol[index + code - first];
            return 1;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return 0;
}

static int fixed_trees(Huffman* literals, Huffman* distances) {
    uint8_t literal_lengths[288];
    uint8_t distance_lengths[32];
    for (uint32_t i = 0; i <= 143u; i++) literal_lengths[i] = 8;
    for (uint32_t i = 144u; i <= 255u; i++) literal_lengths[i] = 9;
    for (uint32_t i = 256u; i <= 279u; i++) literal_lengths[i] = 7;
    for (uint32_t i = 280u; i < 288u; i++) literal_lengths[i] = 8;
    for (uint32_t i = 0; i < 32u; i++) distance_lengths[i] = 5;
    return build_huffman(literals, literal_lengths, 288u) &&
           build_huffman(distances, distance_lengths, 32u);
}

static int dynamic_trees(BitReader* reader,
                         Huffman* literals,
                         Huffman* distances) {
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
    };
    uint32_t value;
    if (!get_bits(reader, 5, &value)) return 0;
    uint32_t literal_count = value + 257u;
    if (!get_bits(reader, 5, &value)) return 0;
    uint32_t distance_count = value + 1u;
    if (!get_bits(reader, 4, &value)) return 0;
    uint32_t code_count = value + 4u;
    if (literal_count > 286u || distance_count > 32u) return 0;
    uint8_t code_lengths[19];
    uint8_t lengths[318];
    os_memset(code_lengths, 0, sizeof(code_lengths));
    os_memset(lengths, 0, sizeof(lengths));
    for (uint32_t i = 0; i < code_count; i++) {
        if (!get_bits(reader, 3, &value)) return 0;
        code_lengths[order[i]] = (uint8_t)value;
    }
    Huffman codes;
    if (!build_huffman(&codes, code_lengths, 19u)) return 0;
    uint32_t at = 0;
    uint32_t total = literal_count + distance_count;
    while (at < total) {
        uint32_t symbol;
        if (!decode_symbol(reader, &codes, &symbol)) return 0;
        if (symbol <= 15u) {
            lengths[at++] = (uint8_t)symbol;
        } else if (symbol == 16u) {
            if (at == 0 || !get_bits(reader, 2, &value)) return 0;
            uint32_t repeat = value + 3u;
            if (repeat > total - at) return 0;
            uint8_t previous = lengths[at - 1u];
            while (repeat-- != 0) lengths[at++] = previous;
        } else if (symbol == 17u || symbol == 18u) {
            uint32_t bits = symbol == 17u ? 3u : 7u;
            uint32_t base = symbol == 17u ? 3u : 11u;
            if (!get_bits(reader, bits, &value)) return 0;
            uint32_t repeat = value + base;
            if (repeat > total - at) return 0;
            while (repeat-- != 0) lengths[at++] = 0;
        } else return 0;
    }
    return lengths[256] != 0 &&
           build_huffman(literals, lengths, literal_count) &&
           build_huffman(distances, lengths + literal_count, distance_count);
}

static int inflate_codes(BitReader* reader,
                         const Huffman* literals,
                         const Huffman* distances,
                         uint8_t* output,
                         uint32_t capacity,
                         uint32_t* output_at) {
    static const uint16_t length_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,
        115,131,163,195,227,258,
    };
    static const uint8_t length_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,
    };
    static const uint16_t distance_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,
    };
    static const uint8_t distance_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,
        12,12,13,13,
    };
    for (;;) {
        uint32_t symbol;
        if (!decode_symbol(reader, literals, &symbol)) return 0;
        if (symbol < 256u) {
            if (*output_at >= capacity) return 0;
            output[(*output_at)++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256u) return 1;
        if (symbol < 257u || symbol > 285u) return 0;
        uint32_t length_index = symbol - 257u;
        uint32_t extra = 0;
        if (!get_bits(reader, length_extra[length_index], &extra)) return 0;
        uint32_t length = length_base[length_index] + extra;
        if (!decode_symbol(reader, distances, &symbol) || symbol >= 30u) return 0;
        if (!get_bits(reader, distance_extra[symbol], &extra)) return 0;
        uint32_t distance = distance_base[symbol] + extra;
        if (distance == 0 || distance > *output_at || length > capacity - *output_at)
            return 0;
        for (uint32_t i = 0; i < length; i++) {
            output[*output_at] = output[*output_at - distance];
            (*output_at)++;
            if (reader->work++ >= PNG_MAX_WORK) return 0;
        }
    }
}

static int inflate_zlib(const uint8_t* bytes,
                        uint32_t size,
                        uint8_t* output,
                        uint32_t capacity) {
    if (size < 6u || (bytes[0] & 0x0Fu) != 8u ||
        (((uint32_t)bytes[0] << 8) | bytes[1]) % 31u != 0 ||
        (bytes[1] & 0x20u) != 0) return 0;
    BitReader reader = {bytes + 2u, size - 6u, 0, 0, 0, 0};
    uint32_t output_at = 0;
    uint32_t final = 0;
    while (!final) {
        uint32_t type;
        if (!get_bits(&reader, 1, &final) || !get_bits(&reader, 2, &type)) return 0;
        if (type == 0u) {
            reader.bits = 0;
            reader.bit_count = 0;
            if (reader.byte_at + 4u > reader.size) return 0;
            uint32_t length = reader.bytes[reader.byte_at] |
                ((uint32_t)reader.bytes[reader.byte_at + 1u] << 8);
            uint32_t inverse = reader.bytes[reader.byte_at + 2u] |
                ((uint32_t)reader.bytes[reader.byte_at + 3u] << 8);
            reader.byte_at += 4u;
            if ((length ^ 0xFFFFu) != inverse || length > capacity - output_at ||
                length > reader.size - reader.byte_at) return 0;
            os_memcpy(output + output_at, reader.bytes + reader.byte_at, length);
            output_at += length;
            reader.byte_at += length;
        } else if (type == 1u || type == 2u) {
            Huffman literals, distances;
            int built = type == 1u ? fixed_trees(&literals, &distances)
                                   : dynamic_trees(&reader, &literals, &distances);
            if (!built || !inflate_codes(&reader, &literals, &distances,
                                         output, capacity, &output_at)) return 0;
        } else return 0;
    }
    if (output_at != capacity) return 0;
    uint32_t s1 = 1u, s2 = 0u;
    for (uint32_t i = 0; i < capacity; i++) {
        s1 = (s1 + output[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return ((s2 << 16) | s1) == be32(bytes + size - 4u);
}

static uint8_t paeth(uint8_t left, uint8_t above, uint8_t upper_left) {
    int32_t p = (int32_t)left + above - upper_left;
    int32_t pa = p > left ? p - left : left - p;
    int32_t pb = p > above ? p - above : above - p;
    int32_t pc = p > upper_left ? p - upper_left : upper_left - p;
    return pa <= pb && pa <= pc ? left : pb <= pc ? above : upper_left;
}

long os_image_decode_png_internal(const uint8_t* bytes,
                                  uint32_t byte_count,
                                  OsImage* image) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (bytes == 0 || image == 0 || byte_count < 33u) return OS_ERR_BAD_BUFFER;
    for (uint32_t i = 0; i < 8u; i++)
        if (bytes[i] != signature[i]) return OS_ERR_BAD_BUFFER;
    uint32_t at = 8u, chunks = 0, width = 0, height = 0;
    uint8_t color_type = 0, channels = 0;
    uint8_t* compressed = 0;
    uint32_t compressed_size = 0;
    int saw_header = 0, saw_end = 0;
    while (at + 12u <= byte_count && chunks++ < PNG_MAX_CHUNKS) {
        uint32_t length = be32(bytes + at);
        if (length > byte_count - at - 12u) goto bad;
        const uint8_t* type = bytes + at + 4u;
        const uint8_t* data = type + 4u;
        if (png_crc(type, length + 4u) != be32(data + length)) goto bad;
        uint32_t name = be32(type);
        if (name == 0x49484452u) {
            if (saw_header || length != 13u) goto bad;
            width = be32(data);
            height = be32(data + 4u);
            color_type = data[9];
            channels = color_type == 2u ? 3u : color_type == 6u ? 4u : 0u;
            if (data[8] != 8u || channels == 0 || data[10] != 0 ||
                data[11] != 0 || data[12] != 0 || width == 0 || height == 0 ||
                width > OS_IMAGE_MAX_DIMENSION || height > OS_IMAGE_MAX_DIMENSION ||
                (uint64_t)width * height > OS_IMAGE_MAX_PIXELS) goto unsupported;
            saw_header = 1;
        } else if (name == 0x49444154u) {
            if (!saw_header || saw_end || length > PNG_MAX_COMPRESSED - compressed_size)
                goto bad;
            uint8_t* next = (uint8_t*)os_realloc(compressed,
                                                 compressed_size + length);
            if (next == 0) goto memory;
            compressed = next;
            os_memcpy(compressed + compressed_size, data, length);
            compressed_size += length;
        } else if (name == 0x49454E44u) {
            if (length != 0 || !saw_header || compressed_size == 0) goto bad;
            saw_end = 1;
            at += 12u;
            break;
        } else if ((type[0] & 0x20u) == 0) {
            goto unsupported;
        }
        at += length + 12u;
    }
    if (!saw_end || at != byte_count) goto bad;
    uint64_t row_bytes64 = (uint64_t)width * channels;
    uint64_t raw_size64 = (row_bytes64 + 1u) * height;
    if (row_bytes64 > UINT32_MAX || raw_size64 > OS_IMAGE_MAX_BYTES) goto bad;
    uint32_t row_bytes = (uint32_t)row_bytes64;
    uint32_t raw_size = (uint32_t)raw_size64;
    uint8_t* raw = (uint8_t*)os_malloc(raw_size);
    uint8_t* filtered = (uint8_t*)os_malloc(row_bytes * height);
    if (raw == 0 || filtered == 0) {
        if (raw) os_free(raw);
        if (filtered) os_free(filtered);
        goto memory;
    }
    if (!inflate_zlib(compressed, compressed_size, raw, raw_size)) {
        os_free(raw); os_free(filtered); goto bad;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = raw[y * (row_bytes + 1u)];
        if (filter > 4u) { os_free(raw); os_free(filtered); goto bad; }
        const uint8_t* source = raw + y * (row_bytes + 1u) + 1u;
        uint8_t* row = filtered + y * row_bytes;
        const uint8_t* above = y == 0 ? 0 : row - row_bytes;
        for (uint32_t x = 0; x < row_bytes; x++) {
            uint8_t left = x < channels ? 0 : row[x - channels];
            uint8_t up = above == 0 ? 0 : above[x];
            uint8_t upper_left = above == 0 || x < channels
                ? 0 : above[x - channels];
            uint8_t predictor = filter == 0 ? 0 : filter == 1 ? left :
                filter == 2 ? up : filter == 3 ? (uint8_t)(((uint32_t)left + up) / 2u) :
                paeth(left, up, upper_left);
            row[x] = (uint8_t)(source[x] + predictor);
        }
    }
    os_free(raw);
    long result = os_image_allocate_internal(width, height, image);
    if (result < 0) { os_free(filtered); os_free(compressed); return result; }
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = filtered + y * row_bytes;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* pixel = row + x * channels;
            image->pixels[y * width + x] = os_image_premultiply_internal(
                pixel[0], pixel[1], pixel[2], channels == 4u ? pixel[3] : 255u);
        }
    }
    os_free(filtered);
    os_free(compressed);
    return OS_SUCCESS;

unsupported:
    if (compressed) os_free(compressed);
    return OS_ERR_UNSUPPORTED;
memory:
    if (compressed) os_free(compressed);
    return OS_ERR_OUT_OF_MEMORY;
bad:
    if (compressed) os_free(compressed);
    return OS_ERR_BAD_BUFFER;
}
