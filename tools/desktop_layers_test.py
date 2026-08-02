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
#include <os64/result.h>

static int failures;

static void check(int condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static OsWindowCreateGeometryRequest request(uint32_t flags) {
    OsWindowCreateGeometryRequest value = {0};
    value.size = sizeof(value);
    value.abi_version = OS64_WINDOW_ABI_VERSION;
    value.command = OS_WINDOW_CREATE;
    value.flags = flags;
    value.request_id = 1;
    value.content_generation = 1;
    value.width = 8;
    value.height = 8;
    value.stride_pixels = 8;
    value.pixel_format = OS64_PIXEL_FORMAT_BGR;
    return value;
}

int main(void) {
    OsWindowCreateGeometryRequest create =
        request(OS_WINDOW_FLAG_LAYER_DESKTOP);
    check(window_protocol_validate_create_geometry(&create) == OS_SUCCESS,
          "desktop flag accepted by parser");
    create = request(OS_WINDOW_FLAG_LAYER_PANEL |
                     OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY);
    check(window_protocol_validate_create_geometry(&create) ==
          OS_ERR_INVALID_ARGUMENT, "multiple privileged layers rejected");
    create = request(1u << 31);
    check(window_protocol_validate_create_geometry(&create) ==
          OS_ERR_INVALID_ARGUMENT, "unknown create flag rejected");

    WindowTable table;
    window_state_init(&table);
    OsProcessIdentity owner = {10, 2};
    WindowEntry* normal1 = window_state_commit_create_layer(
        &table, owner, 1, 0, 0, 8, 8, OS_WINDOW_LAYER_NORMAL);
    WindowEntry* panel = window_state_commit_create_layer(
        &table, owner, 1, 0, 0, 8, 8, OS_WINDOW_LAYER_PANEL);
    WindowEntry* desktop = window_state_commit_create_layer(
        &table, owner, 1, 0, 0, 8, 8, OS_WINDOW_LAYER_DESKTOP);
    WindowEntry* overlay = window_state_commit_create_layer(
        &table, owner, 1, 0, 0, 8, 8, OS_WINDOW_LAYER_SYSTEM_OVERLAY);
    WindowEntry* normal2 = window_state_commit_create_layer(
        &table, owner, 1, 0, 0, 8, 8, OS_WINDOW_LAYER_NORMAL);
    check(normal1 && panel && desktop && overlay && normal2,
          "all layer entries allocated");
    check(table.entries[table.z_slots[0]].layer == OS_WINDOW_LAYER_DESKTOP &&
          table.entries[table.z_slots[1]].layer == OS_WINDOW_LAYER_NORMAL &&
          table.entries[table.z_slots[2]].layer == OS_WINDOW_LAYER_NORMAL &&
          table.entries[table.z_slots[3]].layer == OS_WINDOW_LAYER_PANEL &&
          table.entries[table.z_slots[4]].layer ==
              OS_WINDOW_LAYER_SYSTEM_OVERLAY,
          "layer order independent of creation order");
    window_state_raise(&table, normal1);
    check(table.entries[table.z_slots[2]].window_id == normal1->window_id &&
          table.entries[table.z_slots[3]].layer == OS_WINDOW_LAYER_PANEL,
          "raise remains inside normal layer");
    check(!window_state_accepts_focus(desktop) &&
          window_state_accepts_focus(normal1) &&
          window_state_accepts_focus(panel),
          "desktop is non-focusable");
    window_state_destroy(&table, desktop);
    check(desktop->layer == OS_WINDOW_LAYER_NORMAL,
          "destroy clears privileged layer");
    if (failures) return 1;
    puts("desktop layer policy tests OK");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_desktop_layers_") as tmp:
        source = Path(tmp) / "desktop_layers_test.c"
        binary = Path(tmp) / "desktop_layers_test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user/sdk/include"),
            "-I", str(ROOT / "user/programs/windowd"),
            str(source),
            str(ROOT / "user/programs/windowd/window_protocol.c"),
            str(ROOT / "user/programs/windowd/window_state.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)

    windowd = (ROOT / "user/programs/windowd_c.c").read_text()
    sessiond = (ROOT / "user/programs/sessiond_c.c").read_text()
    serviced = (ROOT / "user/programs/serviced_c.c").read_text()
    required = (
        'os_service_find_owner_identity("session"',
        "window_state_commit_create_layer",
        "window_state_layer_from_flags",
    )
    if any(marker not in windowd for marker in required):
        raise RuntimeError("windowd privileged layer authority is incomplete")
    if 'os_service_register("session"' not in sessiond or \
            "OS_WINDOW_FLAG_LAYER_DESKTOP" not in sessiond:
        raise RuntimeError("sessiond desktop ownership is incomplete")
    if '"window", "windowd_c.elf", "display", "input"' not in serviced or \
            '"session", "sessiond_c.elf", "window", 0' not in serviced:
        raise RuntimeError("session service dependency policy is incomplete")
    print("desktop session/layer integration source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
