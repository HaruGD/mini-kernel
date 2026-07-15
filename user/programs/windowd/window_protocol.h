#ifndef OS64_WINDOWD_PROTOCOL_H
#define OS64_WINDOWD_PROTOCOL_H

#include <stdint.h>

#include <os64/window_types.h>

long window_protocol_validate_create(const OsWindowCreateRequest* request);
long window_protocol_validate_set_surface(const OsWindowSetSurfaceRequest* request);
long window_protocol_validate_damage(const OsWindowDamageRequest* request);
long window_protocol_validate_destroy(const OsWindowDestroyRequest* request);

#endif
