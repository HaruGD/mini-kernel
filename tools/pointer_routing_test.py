#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/mouse.h"
#include "window_state.h"
#include "window_interaction.h"

static OsProcessIdentity owner(uint32_t pid) {
    OsProcessIdentity value = {pid, pid + 10};
    return value;
}

int main(void) {
    Ps2MousePacketState packet;
    OsInputEvent event;
    ps2_mouse_packet_init(&packet, 1);
    assert(!ps2_mouse_packet_push(&packet, 0x00, 1, &event));
    assert(!ps2_mouse_packet_push(&packet, 0x09, 2, &event));
    assert(!ps2_mouse_packet_push(&packet, 5, 2, &event));
    assert(!ps2_mouse_packet_push(&packet, 0xFD, 2, &event));
    assert(ps2_mouse_packet_push(&packet, 1, 2, &event));
    assert(event.type == OS_INPUT_EVENT_POINTER);
    assert(event.data.pointer.type == OS_POINTER_EVENT_BUTTON_DOWN);
    assert(event.data.pointer.delta_x == 5 && event.data.pointer.delta_y == 3);
    assert(event.data.pointer.wheel_delta == 1);
    assert(event.data.pointer.buttons == OS_POINTER_BUTTON_LEFT);
    assert(event.data.pointer.changed_buttons == OS_POINTER_BUTTON_LEFT);

    WindowTable table;
    window_state_init(&table);
    WindowEntry* back = window_state_commit_create(&table, owner(1), 1,
                                                    20, 40, 180, 100);
    WindowEntry* front = window_state_commit_create(&table, owner(2), 1,
                                                     60, 70, 120, 80);
    assert(back && front);
    window_state_set_decorated(back, 1);
    window_state_set_decorated(front, 1);

    WindowInteraction interaction;
    window_interaction_init(&interaction, 320, 200);
    OsPointerEvent raw = {OS_POINTER_EVENT_MOVE,
                          OS_POINTER_POSITION_UNKNOWN,
                          OS_POINTER_POSITION_UNKNOWN,
                          10000, -10000, 0, 0, 0};
    OsPointerEvent normalized;
    assert(window_interaction_normalize(&interaction, &raw, &normalized) == 0);
    assert(normalized.x == 319 && normalized.y == 0);

    WindowHit hit = window_interaction_hit_test(&table, 80, 80);
    assert(hit.entry == front && hit.region == WINDOW_HIT_CONTENT);
    hit = window_interaction_hit_test(&table, 30, 25);
    assert(hit.entry == back && hit.region == WINDOW_HIT_TITLE);
    hit = window_interaction_hit_test(&table, 185, 25);
    assert(hit.entry == back && hit.region == WINDOW_HIT_CLOSE);

    interaction.pointer_x = 30;
    interaction.pointer_y = 25;
    window_interaction_capture(&interaction, back, WINDOW_CAPTURE_MOVE,
                               WINDOW_HIT_TITLE);
    interaction.pointer_x = 80;
    interaction.pointer_y = 65;
    assert(window_interaction_update_geometry(&interaction, back));
    assert(back->x == 70 && back->y == 80);

    interaction.pointer_x = back->x + (int32_t)back->width + 3;
    interaction.pointer_y = back->y + (int32_t)back->height + 3;
    window_interaction_capture(&interaction, back, WINDOW_CAPTURE_RESIZE,
                               WINDOW_HIT_RESIZE_RIGHT |
                               WINDOW_HIT_RESIZE_BOTTOM);
    interaction.pointer_x -= 1000;
    interaction.pointer_y -= 1000;
    assert(window_interaction_update_geometry(&interaction, back));
    assert(back->width == OS_WINDOW_MIN_WIDTH);
    assert(back->height == OS_WINDOW_MIN_HEIGHT);

    uint32_t stale_generation = back->window_generation;
    window_interaction_capture(&interaction, back, WINDOW_CAPTURE_CLIENT,
                               WINDOW_HIT_CONTENT);
    window_state_destroy(&table, back);
    assert(window_interaction_capture_target(&interaction, &table) == 0);
    assert(interaction.capture_mode == WINDOW_CAPTURE_NONE);
    window_interaction_cancel_target(&interaction, back->window_id,
                                     stale_generation);
    puts("pointer packet/routing/interaction tests OK");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64-pointer-") as directory:
        source = Path(directory) / "pointer_test.c"
        binary = Path(directory) / "pointer_test"
        source.write_text(HARNESS)
        command = [
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user" / "sdk" / "include"),
            "-I", str(ROOT / "user" / "programs" / "windowd"),
            str(source),
            str(ROOT / "drivers" / "input" / "ps2_mouse" / "mouse_packet.c"),
            str(ROOT / "user" / "programs" / "windowd" / "window_state.c"),
            str(ROOT / "user" / "programs" / "windowd" / "window_interaction.c"),
            "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    text = (ROOT / "user" / "programs" / "inputd_c.c").read_text()
    assert "OS_SERVICE_CAP_KEYBOARD | OS_SERVICE_CAP_POINTER" in text
    assert "event->type != OS_INPUT_EVENT_POINTER" in text
    windowd = (ROOT / "user" / "programs" / "windowd_c.c").read_text()
    assert "window_interaction_hit_test" in windowd
    assert "OS_WINDOW_EVENT_CLOSE_REQUEST" in windowd
    assert "draw_cursor" in windowd
    print("pointer service/driver integration source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
