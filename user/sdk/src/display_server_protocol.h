#ifndef OS64_DISPLAY_SERVER_PROTOCOL_H
#define OS64_DISPLAY_SERVER_PROTOCOL_H

#include <stdint.h>

#include "os64/display_types.h"
#include "os64/process_types.h"

typedef struct OsDisplayPresentTransaction {
    uint32_t active;
    uint32_t last_accepted_generation;
    OsProcessIdentity sender;
    uint32_t request_id;
    uint32_t frame_generation;
    uint32_t flags;
    uint32_t expected_rects;
    uint32_t expected_chunks;
    uint32_t next_chunk;
    uint32_t rect_count;
    OsRect rects[OS_DISPLAY_DAMAGE_MAX_RECTS];
} OsDisplayPresentTransaction;

void os_display_server_protocol_init(OsDisplayPresentTransaction* transaction);
void os_display_server_protocol_abort(OsDisplayPresentTransaction* transaction);
int os_display_server_protocol_should_replace(
    const OsDisplayPresentTransaction* transaction,
    const OsDisplayPresentBegin* begin);
long os_display_server_protocol_begin(OsDisplayPresentTransaction* transaction,
                                      OsProcessIdentity sender,
                                      const OsDisplayPresentBegin* begin);
long os_display_server_protocol_damage(OsDisplayPresentTransaction* transaction,
                                       OsProcessIdentity sender,
                                       const OsDisplayPresentDamage* damage);
long os_display_server_protocol_commit(const OsDisplayPresentTransaction* transaction,
                                       OsProcessIdentity sender,
                                       const OsDisplayPresentCommit* commit);
void os_display_server_protocol_accept(OsDisplayPresentTransaction* transaction);

#endif
