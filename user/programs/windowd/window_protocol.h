#ifndef OS64_WINDOWD_PROTOCOL_H
#define OS64_WINDOWD_PROTOCOL_H

#include <stdint.h>

#include <os64/window_types.h>

long window_protocol_validate_create(const OsWindowCreateRequest* request);
long window_protocol_validate_create_geometry(
    const OsWindowCreateGeometryRequest* request);
long window_protocol_validate_set_surface(const OsWindowSetSurfaceRequest* request);
long window_protocol_validate_damage(const OsWindowDamageRequest* request);
long window_protocol_validate_destroy(const OsWindowDestroyRequest* request);
long window_protocol_validate_state(const OsWindowStateRequest* request,
                                    uint32_t command);
long window_protocol_validate_move(const OsWindowMoveRequest* request);
long window_protocol_validate_resize(const OsWindowResizeRequest* request);
long window_protocol_validate_damage_begin(
    const OsWindowDamageBeginRequest* request);
long window_protocol_validate_damage_rects(
    const OsWindowDamageRectsRequest* request);
long window_protocol_validate_damage_commit(
    const OsWindowDamageCommitRequest* request);
long window_protocol_validate_info(const OsWindowInfoRequest* request);

#endif
