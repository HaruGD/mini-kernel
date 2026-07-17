#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>

#include "window_protocol.h"
#include "window_state.h"
#include "window_compositor.h"
#include <os64/graphics_types.h>
#include <os64/result.h>

static int failures;

static void check(int condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static OsWindowCreateRequest valid_create(void) {
    OsWindowCreateRequest request = {0};
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_CREATE;
    request.request_id = 7;
    request.content_generation = 1;
    request.width = 4;
    request.height = 3;
    request.stride_pixels = 4;
    request.pixel_format = OS64_PIXEL_FORMAT_BGR;
    return request;
}

int main(void) {
    OsWindowCreateRequest create = valid_create();
    check(window_protocol_validate_create(&create) == OS_SUCCESS, "valid create");
    create.abi_version++;
    check(window_protocol_validate_create(&create) == OS_ERR_INVALID_ARGUMENT,
          "malformed ABI");
    create = valid_create();
    create.stride_pixels = 3;
    check(window_protocol_validate_create(&create) == OS_ERR_INVALID_ARGUMENT,
          "short stride");

    OsWindowSetSurfaceRequest replace = {0};
    replace.size = sizeof(replace);
    replace.abi_version = OS64_WINDOW_ABI_VERSION;
    replace.command = OS_WINDOW_SET_SURFACE;
    replace.request_id = 8;
    replace.window_id = 1;
    replace.window_generation = 1;
    replace.content_generation = 2;
    replace.width = 4;
    replace.height = 3;
    replace.stride_pixels = 4;
    replace.pixel_format = OS64_PIXEL_FORMAT_BGR;
    check(window_protocol_validate_set_surface(&replace) == OS_SUCCESS,
          "valid set surface");

    OsWindowDamageRequest damage = {
        sizeof(OsWindowDamageRequest), OS64_WINDOW_ABI_VERSION,
        OS_WINDOW_DAMAGE, 0, 9, 1, 1, 2
    };
    check(window_protocol_validate_damage(&damage) == OS_SUCCESS, "valid damage");
    damage.content_generation = 0;
    check(window_protocol_validate_damage(&damage) == OS_ERR_INVALID_ARGUMENT,
          "zero damage generation");

    OsWindowDestroyRequest destroy = {
        sizeof(OsWindowDestroyRequest), OS64_WINDOW_ABI_VERSION,
        OS_WINDOW_DESTROY, 0, 10, 1, 1, 0
    };
    check(window_protocol_validate_destroy(&destroy) == OS_SUCCESS, "valid destroy");
    destroy.reserved = 1;
    check(window_protocol_validate_destroy(&destroy) == OS_ERR_INVALID_ARGUMENT,
          "destroy reserved field");

    OsWindowInfoRequest info = {
        sizeof(OsWindowInfoRequest), OS64_WINDOW_ABI_VERSION,
        OS_WINDOW_GET_INFO, 0, 11, 0, 0, 0
    };
    check(window_protocol_validate_info(&info) == OS_SUCCESS,
          "valid service information query");
    info.window_id = 1;
    check(window_protocol_validate_info(&info) == OS_ERR_INVALID_ARGUMENT,
          "partial information identity rejected");
    info.window_generation = 1;
    check(window_protocol_validate_info(&info) == OS_SUCCESS,
          "valid window information query");

    WindowTable state;
    OsProcessIdentity owner = {20, 3};
    OsProcessIdentity attacker = {21, 4};
    window_state_init(&state);
    check(window_state_can_create(&state, owner, 1, 4, 3) == OS_SUCCESS,
          "empty state accepts create");
    WindowEntry* entry = window_state_commit_create(&state, owner, 1,
                                                     0, 0, 4, 3);
    check(entry != NULL && entry->active &&
          entry->window_id == OS_WINDOW_ID_FULLSCREEN &&
          entry->window_generation == 1, "create commit");
    WindowEntry* found = NULL;
    check(window_state_validate_target(&state, attacker, 1, 1, &found) ==
          OS_ERR_PERMISSION_DENIED, "wrong owner denied");
    check(window_state_validate_target(&state, owner, 1, 2, &found) ==
          OS_ERR_NOT_FOUND,
          "stale generation denied");
    check(window_state_validate_content(&state, owner, 1, 1, 1, &found) ==
          OS_ERR_ALREADY_EXISTS, "duplicate content denied");
    check(window_state_validate_content(&state, owner, 1, 1, 2, &found) ==
          OS_SUCCESS,
          "new content accepted");
    window_state_commit_content(entry, 2);
    check(entry->accepted_content_generation == 2, "content commit");
    window_state_destroy(&state, entry);
    check(!entry->active && entry->owner.pid == 0, "destroy cleanup");
    entry = window_state_commit_create(&state, attacker, 1, 0, 0, 4, 3);
    check(entry != NULL && entry->window_generation == 2,
          "window generation advances");

    const uint32_t source[6] = {
        0xFF112233u, 0x00445566u, 0,
        0xAA778899u, 0x00AABBCCu, 0,
    };
    uint32_t destination[8] = {0};
    check(window_compositor_copy_full(destination, 4, source, 3, 2, 2) ==
          OS_SUCCESS, "opaque full copy");
    check(destination[0] == 0x00112233u && destination[1] == 0x00445566u &&
          destination[4] == 0x00778899u && destination[5] == 0x00AABBCCu,
          "copy pixels and strip alpha");
    check(window_compositor_copy_full(destination, 1, source, 3, 2, 2) ==
          OS_ERR_INVALID_ARGUMENT, "destination stride bound");
    window_compositor_clear(destination, 4, 2, 2, 0xFF010203u);
    check(destination[0] == 0x00010203u && destination[5] == 0x00010203u,
          "clear opaque pixels");

    if (failures != 0) {
        return 1;
    }
    puts("single-window protocol/state/compositor tests OK");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_window_single_") as directory:
        source = Path(directory) / "window_single_test.c"
        binary = Path(directory) / "window_single_test"
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
    serviced = (ROOT / "user/programs/serviced_c.c").read_text(encoding="utf-8")
    if "os_gfx_present_surface" in windowd or "os_gfx_get_info" in windowd:
        raise RuntimeError("windowd acquired a direct display path")
    if "display reconnect full frame submitted" not in windowd or \
            "pending_full_frame" not in windowd:
        raise RuntimeError("windowd display reconnect/resubmit path is missing")
    if '{"window", "windowd_c.elf", "display"' not in serviced or \
            "OS_PROCESS_PERMISSION_PROFILE_GUI_SERVICE" not in serviced:
        raise RuntimeError("windowd supervision policy is missing")
    print("single-window authority/supervision source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
