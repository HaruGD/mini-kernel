#ifndef OS64_SDK_DISPLAY_H
#define OS64_SDK_DISPLAY_H

#include <stdint.h>

#include "os64/display_types.h"
#include "os64/handle_types.h"
#include "os64/process_types.h"

long os_display_present(OsProcessIdentity display,
                        OsHandle surface,
                        uint32_t frame_generation,
                        const OsRect* rects,
                        uint32_t rect_count,
                        uint32_t timeout_ticks,
                        OsDisplayPresentReply* reply);

#endif
