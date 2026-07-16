#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "window_protocol.h"
#include "window_state.h"
#include "window_compositor.h"
#include <os64/result.h>

static int failures;

static void check(int condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static uint64_t hash_pixels(const uint32_t* pixels, uint32_t count) {
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t i = 0; i < count; i++) {
        hash ^= pixels[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void fill(uint32_t* pixels, uint32_t count, uint32_t color) {
    for (uint32_t i = 0; i < count; i++) pixels[i] = color;
}

int main(void) {
    check(OS_WINDOW_MAX_WINDOWS == 12, "window capacity ABI");
    check(OS_WINDOW_DAMAGE_MAX_RECTS == 16, "window damage bound ABI");
    check(sizeof(OsWindowDamageRectsRequest) == 96, "damage chunk payload ABI");

    OsWindowCreateGeometryRequest create = {0};
    create.size = sizeof(create);
    create.abi_version = OS64_WINDOW_ABI_VERSION;
    create.command = OS_WINDOW_CREATE;
    create.request_id = 10;
    create.content_generation = 1;
    create.x = -20;
    create.y = 30;
    create.width = 4;
    create.height = 3;
    create.stride_pixels = 4;
    create.pixel_format = OS64_PIXEL_FORMAT_BGR;
    check(window_protocol_validate_create_geometry(&create) == OS_SUCCESS,
          "geometry create valid");
    create.stride_pixels = 3;
    check(window_protocol_validate_create_geometry(&create) ==
          OS_ERR_INVALID_ARGUMENT, "geometry short stride rejected");

    OsWindowDamageBeginRequest begin = {0};
    begin.size = sizeof(begin);
    begin.abi_version = OS64_WINDOW_ABI_VERSION;
    begin.command = OS_WINDOW_DAMAGE_BEGIN;
    begin.request_id = 11;
    begin.window_id = 1;
    begin.window_generation = 1;
    begin.content_generation = 2;
    begin.submission_id = 99;
    begin.rect_count = 5;
    begin.chunk_count = 2;
    check(window_protocol_validate_damage_begin(&begin) == OS_SUCCESS,
          "damage begin valid");
    begin.chunk_count = 1;
    check(window_protocol_validate_damage_begin(&begin) ==
          OS_ERR_INVALID_ARGUMENT, "damage chunk total mismatch rejected");

    OsWindowDamageRectsRequest chunks = {0};
    chunks.size = sizeof(chunks);
    chunks.abi_version = OS64_WINDOW_ABI_VERSION;
    chunks.command = OS_WINDOW_DAMAGE_RECTS;
    chunks.request_id = 11;
    chunks.submission_id = 99;
    chunks.rect_count = 1;
    chunks.rects[0] = (OsRect){-2, -2, 5, 5};
    check(window_protocol_validate_damage_rects(&chunks) == OS_SUCCESS,
          "clippable damage valid");
    chunks.rects[1] = (OsRect){1, 1, 1, 1};
    check(window_protocol_validate_damage_rects(&chunks) ==
          OS_ERR_INVALID_ARGUMENT, "unused chunk payload rejected");
    chunks.rects[1] = (OsRect){0, 0, 0, 0};
    chunks.rects[0].width = 0;
    check(window_protocol_validate_damage_rects(&chunks) ==
          OS_ERR_INVALID_ARGUMENT, "empty rectangle rejected");

    WindowTable capacity;
    window_state_init(&capacity);
    for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
        OsProcessIdentity owner = {100 + i, 1 + i};
        WindowEntry* entry = window_state_commit_create(&capacity, owner, 1,
                                                         (int32_t)i, 0, 2, 2);
        check(entry != NULL && entry->window_id == i + 1,
              "fixed capacity allocation");
    }
    OsProcessIdentity excess = {500, 9};
    check(window_state_can_create(&capacity, excess, 1, 2, 2) ==
          OS_ERR_NO_RESOURCES, "thirteenth window rejected");
    WindowEntry* reused = &capacity.entries[4];
    uint32_t generation = reused->window_generation;
    window_state_destroy(&capacity, reused);
    reused = window_state_commit_create(&capacity, excess, 1, 5, 6, 2, 2);
    check(reused != NULL && reused->window_id == 5 &&
          reused->window_generation == generation + 1,
          "slot generation advances on reuse");
    window_state_set_visible(&capacity, &capacity.entries[1], 0, 0);
    check(!capacity.entries[1].visible, "hide state");
    window_state_set_visible(&capacity, &capacity.entries[1], 1, 1);
    check(capacity.entries[1].visible &&
          capacity.z_slots[capacity.count - 1] == 1, "show raises stable z-order");

    WindowTable table;
    window_state_init(&table);
    OsProcessIdentity back_owner = {20, 3};
    OsProcessIdentity front_owner = {21, 4};
    WindowEntry* back = window_state_commit_create(&table, back_owner, 1,
                                                    1, 1, 5, 4);
    WindowEntry* front = window_state_commit_create(&table, front_owner, 1,
                                                     3, 2, 4, 3);
    check(back != NULL && front != NULL && table.count == 2,
          "two overlapping windows");
    WindowEntry* found = NULL;
    check(window_state_validate_target(&table, back_owner,
                                       back->window_id,
                                       back->window_generation, &found) ==
          OS_SUCCESS && found == back, "owner lookup");
    check(window_state_validate_target(&table, front_owner,
                                       back->window_id,
                                       back->window_generation, &found) ==
          OS_ERR_PERMISSION_DENIED, "cross-owner denial");

    uint32_t back_pixels[20];
    uint32_t front_pixels[12];
    uint32_t screen[48];
    fill(back_pixels, 20, 0x001122CCu);
    fill(front_pixels, 12, 0x00CC3311u);
    fill(screen, 48, 0x00DEADBEu);
    WindowCompositorSource sources[OS_WINDOW_MAX_WINDOWS] = {0};
    sources[back->window_id - 1] = (WindowCompositorSource){back_pixels, 5};
    sources[front->window_id - 1] = (WindowCompositorSource){front_pixels, 4};
    WindowDamageAccumulator damage;
    window_damage_init(&damage, 8, 6);
    window_damage_full(&damage);
    check(window_compositor_compose(screen, 8, 8, 6, 0x00010203u,
                                    &table, sources, &damage) == OS_SUCCESS,
          "full multiwindow compose");
    check(screen[0] == 0x00010203u, "background pixel");
    check(screen[1 + 1 * 8] == 0x001122CCu, "back window pixel");
    check(screen[3 + 2 * 8] == 0x00CC3311u, "front overlap pixel");
    uint64_t overlap_hash = hash_pixels(screen, 48);

    window_damage_reset(&damage);
    window_damage_add_screen(&damage, window_state_screen_rect(front));
    window_state_set_visible(&table, front, 0, 0);
    window_compositor_compose(screen, 8, 8, 6, 0x00010203u,
                              &table, sources, &damage);
    check(screen[3 + 2 * 8] == 0x001122CCu, "hide reveals lower window");
    check(hash_pixels(screen, 48) != overlap_hash, "hide changes frame hash");

    window_state_set_visible(&table, front, 1, 1);
    window_state_move(front, -2, -1);
    window_damage_full(&damage);
    window_compositor_compose(screen, 8, 8, 6, 0x00010203u,
                              &table, sources, &damage);
    check(screen[0] == 0x00CC3311u && screen[1 + 1 * 8] == 0x00CC3311u,
          "left and top clipping");

    window_state_move(front, 7, 5);
    window_damage_full(&damage);
    window_compositor_compose(screen, 8, 8, 6, 0x00010203u,
                              &table, sources, &damage);
    check(screen[7 + 5 * 8] == 0x00CC3311u,
          "right and bottom clipping");

    window_state_move(front, 3, 2);
    front_pixels[1 + 1 * 4] = 0x0000EE77u;
    window_damage_reset(&damage);
    check(window_damage_add_window(&damage, front, (OsRect){1, 1, 1, 1}) ==
          OS_SUCCESS && damage.count == 1, "partial damage translation");
    uint32_t untouched = screen[3 + 2 * 8];
    window_compositor_compose(screen, 8, 8, 6, 0x00010203u,
                              &table, sources, &damage);
    check(screen[4 + 3 * 8] == 0x0000EE77u, "partial damage copied");
    check(screen[3 + 2 * 8] == untouched, "outside partial damage untouched");

    WindowDamageAccumulator overflow;
    window_damage_init(&overflow, 256, 256);
    for (uint32_t i = 0; i < 65; i++) {
        OsRect rect = {(int32_t)((i % 13) * 4),
                       (int32_t)((i / 13) * 4), 1, 1};
        window_damage_add_screen(&overflow, rect);
    }
    check(overflow.full_screen && overflow.count == 1 &&
          overflow.rects[0].width == 256 && overflow.rects[0].height == 256,
          "screen damage overflow collapses to full frame");

    WindowDamageAccumulator arithmetic;
    window_damage_init(&arithmetic, 8, 6);
    check(window_damage_add_screen(&arithmetic,
                                   (OsRect){INT32_MAX - 2, INT32_MAX - 2, 8, 8}) ==
          OS_SUCCESS && arithmetic.count == 0,
          "rectangle arithmetic overflow clips safely");

    if (failures != 0) return 1;
    printf("multiwindow protocol/state/compositor tests OK hash=%016llx\n",
           (unsigned long long)overlap_hash);
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_window_multi_") as directory:
        source = Path(directory) / "window_multi_test.c"
        binary = Path(directory) / "window_multi_test"
        source.write_text(HARNESS, encoding="utf-8")
        command = [
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user/sdk/include"),
            "-I", str(ROOT / "user/programs/windowd"),
            str(source),
            str(ROOT / "user/programs/windowd/window_protocol.c"),
            str(ROOT / "user/programs/windowd/window_state.c"),
            str(ROOT / "user/programs/windowd/window_compositor.c"),
            "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

    windowd = (ROOT / "user/programs/windowd_c.c").read_text(encoding="utf-8")
    required = (
        "OS_WINDOW_MAX_WINDOWS",
        "WindowDamageTransaction",
        "window_damage_add_window",
        "window_state_set_visible",
        "window_state_move",
        "window_state_resize",
        "rebuild_composite",
    )
    if any(marker not in windowd for marker in required):
        raise RuntimeError("windowd multiwindow integration is incomplete")
    if "os_gfx_present_surface" in windowd or "os_gfx_get_info" in windowd:
        raise RuntimeError("windowd acquired a direct display path")
    print("multiwindow service integration source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
