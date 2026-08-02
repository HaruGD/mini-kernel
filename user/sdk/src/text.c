#include "os64/result.h"
#include "os64/text.h"

long os_utf8_next(const char* text,
                  uint32_t length,
                  uint32_t* index,
                  uint32_t* codepoint) {
    if (text == 0 || index == 0 || codepoint == 0 || *index > length)
        return OS_ERR_INVALID_ARGUMENT;
    if (*index == length) return OS_ERR_NOT_FOUND;
    uint32_t at = *index;
    uint8_t first = (uint8_t)text[at++];
    if (first < 0x80u) {
        *index = at;
        *codepoint = first;
        return OS_SUCCESS;
    }
    uint32_t needed;
    uint32_t value;
    uint32_t minimum;
    if (first >= 0xC2u && first <= 0xDFu) {
        needed = 1; value = first & 0x1Fu; minimum = 0x80u;
    } else if (first >= 0xE0u && first <= 0xEFu) {
        needed = 2; value = first & 0x0Fu; minimum = 0x800u;
    } else if (first >= 0xF0u && first <= 0xF4u) {
        needed = 3; value = first & 0x07u; minimum = 0x10000u;
    } else {
        *index = *index + 1u;
        *codepoint = OS_UNICODE_REPLACEMENT;
        return OS_SUCCESS;
    }
    if (needed > length - at) {
        *index = *index + 1u;
        *codepoint = OS_UNICODE_REPLACEMENT;
        return OS_SUCCESS;
    }
    for (uint32_t i = 0; i < needed; i++) {
        uint8_t continuation = (uint8_t)text[at + i];
        if ((continuation & 0xC0u) != 0x80u) {
            *index = *index + 1u;
            *codepoint = OS_UNICODE_REPLACEMENT;
            return OS_SUCCESS;
        }
        value = (value << 6) | (continuation & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        *index = *index + 1u;
        *codepoint = OS_UNICODE_REPLACEMENT;
        return OS_SUCCESS;
    }
    *index = at + needed;
    *codepoint = value;
    return OS_SUCCESS;
}
