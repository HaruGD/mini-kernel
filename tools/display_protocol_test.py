#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "display_server_protocol.h"
#include "os64/result.h"

static int failures;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static OsDisplayPresentBegin make_begin(uint32_t generation,
                                        uint32_t rect_count,
                                        uint32_t flags) {
    OsDisplayPresentBegin value = {0};
    value.size = sizeof(value);
    value.abi_version = OS64_DISPLAY_ABI_VERSION;
    value.command = OS_DISPLAY_PRESENT_BEGIN;
    value.flags = flags;
    value.request_id = generation + 100;
    value.frame_generation = generation;
    value.width = 800;
    value.height = 600;
    value.stride_pixels = 800;
    value.pixel_format = OS64_PIXEL_FORMAT_RGB;
    value.rect_count = rect_count;
    value.chunk_count = rect_count == 0 ? 0 :
        (rect_count + OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK - 1) /
            OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK;
    return value;
}

static OsDisplayPresentDamage make_damage(const OsDisplayPresentBegin* begin,
                                          uint32_t chunk,
                                          uint32_t count) {
    OsDisplayPresentDamage value = {0};
    value.size = sizeof(value);
    value.abi_version = OS64_DISPLAY_ABI_VERSION;
    value.command = OS_DISPLAY_PRESENT_DAMAGE;
    value.request_id = begin->request_id;
    value.frame_generation = begin->frame_generation;
    value.chunk_index = chunk;
    value.rect_count = count;
    for (uint32_t i = 0; i < count; i++) {
        value.rects[i].x = (int32_t)(chunk * 40 + i * 8);
        value.rects[i].y = (int32_t)(chunk * 20 + i * 4);
        value.rects[i].width = 4;
        value.rects[i].height = 3;
    }
    return value;
}

static OsDisplayPresentCommit make_commit(const OsDisplayPresentBegin* begin) {
    OsDisplayPresentCommit value = {0};
    value.size = sizeof(value);
    value.abi_version = OS64_DISPLAY_ABI_VERSION;
    value.command = OS_DISPLAY_PRESENT_COMMIT;
    value.request_id = begin->request_id;
    value.frame_generation = begin->frame_generation;
    return value;
}

int main(void) {
    OsProcessIdentity sender = {17, 3};
    OsProcessIdentity stranger = {18, 4};
    OsDisplayPresentTransaction transaction;
    os_display_server_protocol_init(&transaction);

    OsDisplayPresentBegin begin = make_begin(7, 6, 0);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) == OS_SUCCESS);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) == OS_ERR_NOT_READY);
    OsDisplayPresentDamage first = make_damage(&begin, 0, 4);
    OsDisplayPresentDamage second = make_damage(&begin, 1, 2);
    check(os_display_server_protocol_damage(&transaction, stranger, &first) ==
          OS_ERR_PERMISSION_DENIED);
    check(transaction.active == 1 && transaction.rect_count == 0);
    check(os_display_server_protocol_damage(&transaction, sender, &first) == OS_SUCCESS);
    check(os_display_server_protocol_damage(&transaction, sender, &first) ==
          OS_ERR_INVALID_ARGUMENT);
    OsDisplayPresentCommit commit = make_commit(&begin);
    check(os_display_server_protocol_commit(&transaction, sender, &commit) ==
          OS_ERR_INVALID_ARGUMENT);
    check(os_display_server_protocol_damage(&transaction, sender, &second) == OS_SUCCESS);
    check(transaction.rect_count == 6 && transaction.next_chunk == 2);
    check(os_display_server_protocol_commit(&transaction, stranger, &commit) ==
          OS_ERR_PERMISSION_DENIED);
    check(os_display_server_protocol_commit(&transaction, sender, &commit) == OS_SUCCESS);
    os_display_server_protocol_accept(&transaction);
    check(transaction.active == 0 && transaction.last_accepted_generation == 7);

    begin = make_begin(7, 0, OS_DISPLAY_PRESENT_FLAG_FULL_FRAME);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_ALREADY_EXISTS);
    begin = make_begin(8, 0, OS_DISPLAY_PRESENT_FLAG_FULL_FRAME);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) == OS_SUCCESS);
    commit = make_commit(&begin);
    check(os_display_server_protocol_commit(&transaction, sender, &commit) == OS_SUCCESS);
    os_display_server_protocol_accept(&transaction);

    begin = make_begin(9, OS_DISPLAY_DAMAGE_MAX_RECTS + 1, 0);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_INVALID_ARGUMENT);
    begin = make_begin(9, 1, OS_DISPLAY_PRESENT_FLAG_FULL_FRAME);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_INVALID_ARGUMENT);
    begin = make_begin(9, 1, 0);
    begin.abi_version++;
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_INVALID_ARGUMENT);
    begin = make_begin(9, 1, 0);
    begin.pixel_format = 99;
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_INVALID_ARGUMENT);
    begin = make_begin(9, 1, 0);
    begin.width = 0;
    check(os_display_server_protocol_begin(&transaction, sender, &begin) ==
          OS_ERR_INVALID_ARGUMENT);

    begin = make_begin(10, 5, 0);
    check(os_display_server_protocol_begin(&transaction, sender, &begin) == OS_SUCCESS);
    OsDisplayPresentBegin replacement =
        make_begin(11, 0, OS_DISPLAY_PRESENT_FLAG_FULL_FRAME);
    check(os_display_server_protocol_should_replace(&transaction, &replacement) == 1);
    replacement.frame_generation = 10;
    check(os_display_server_protocol_should_replace(&transaction, &replacement) == 0);
    replacement = make_begin(11, 1, 0);
    check(os_display_server_protocol_should_replace(&transaction, &replacement) == 0);
    second = make_damage(&begin, 1, 1);
    check(os_display_server_protocol_damage(&transaction, sender, &second) ==
          OS_ERR_INVALID_ARGUMENT);
    first = make_damage(&begin, 0, 4);
    first.rects[2].width = 0;
    check(os_display_server_protocol_damage(&transaction, sender, &first) ==
          OS_ERR_INVALID_ARGUMENT);
    os_display_server_protocol_abort(&transaction);
    check(transaction.active == 0 && transaction.last_accepted_generation == 8);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_display_protocol_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "display_protocol_test.c"
        binary_path = temp_path / "display_protocol_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        subprocess.run(
            [
                "gcc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO_ROOT / "include"),
                "-I",
                str(REPO_ROOT / "user/sdk/include"),
                "-I",
                str(REPO_ROOT / "user/sdk/src"),
                str(REPO_ROOT / "user/sdk/src/display_server_protocol.c"),
                str(source_path),
                "-o",
                str(binary_path),
            ],
            check=True,
        )
        subprocess.run([str(binary_path)], check=True)
    print("display protocol test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
