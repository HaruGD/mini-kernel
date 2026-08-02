#ifndef OS64_TEXT_H
#define OS64_TEXT_H

#include <stdint.h>

#define OS_UNICODE_REPLACEMENT 0xFFFDU

long os_utf8_next(const char* text,
                  uint32_t length,
                  uint32_t* index,
                  uint32_t* codepoint);

#endif
