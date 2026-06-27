#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/input/input_event_queue.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static OsInputEvent make_key_event(uint32_t sequence) {
    OsInputEvent event;
    event.type = OS_INPUT_EVENT_KEY;
    event.size = sizeof(OsInputEvent);
    event.timestamp_ticks = 1000u + sequence;
    event.data.key.type = OS_KEY_EVENT_DOWN;
    event.data.key.keycode = sequence;
    event.data.key.modifiers = sequence & OS_KEY_MOD_SHIFT;
    event.data.key.character = 'a' + (sequence % 26u);
    return event;
}

static OsInputEvent make_pointer_event(uint32_t sequence) {
    OsInputEvent event;
    event.type = OS_INPUT_EVENT_POINTER;
    event.size = sizeof(OsInputEvent);
    event.timestamp_ticks = 2000u + sequence;
    event.data.pointer.type = OS_POINTER_EVENT_BUTTON_DOWN;
    event.data.pointer.x = 320 + (int32_t)sequence;
    event.data.pointer.y = 200 - (int32_t)sequence;
    event.data.pointer.delta_x = -3;
    event.data.pointer.delta_y = 5;
    event.data.pointer.wheel_delta = -1;
    event.data.pointer.buttons = OS_POINTER_BUTTON_LEFT | OS_POINTER_BUTTON_X1;
    event.data.pointer.changed_buttons = OS_POINTER_BUTTON_LEFT;
    return event;
}

static void expect_key_event(const OsInputEvent* event, uint32_t sequence) {
    check(event->type == OS_INPUT_EVENT_KEY);
    check(event->size == sizeof(OsInputEvent));
    check(event->timestamp_ticks == 1000u + sequence);
    check(event->data.key.type == OS_KEY_EVENT_DOWN);
    check(event->data.key.keycode == sequence);
    check(event->data.key.modifiers == (sequence & OS_KEY_MOD_SHIFT));
    check(event->data.key.character == 'a' + (sequence % 26u));
}

static void expect_pointer_event(const OsInputEvent* event, uint32_t sequence) {
    check(event->type == OS_INPUT_EVENT_POINTER);
    check(event->size == sizeof(OsInputEvent));
    check(event->timestamp_ticks == 2000u + sequence);
    check(event->data.pointer.type == OS_POINTER_EVENT_BUTTON_DOWN);
    check(event->data.pointer.x == 320 + (int32_t)sequence);
    check(event->data.pointer.y == 200 - (int32_t)sequence);
    check(event->data.pointer.delta_x == -3);
    check(event->data.pointer.delta_y == 5);
    check(event->data.pointer.wheel_delta == -1);
    check(event->data.pointer.buttons == (OS_POINTER_BUTTON_LEFT | OS_POINTER_BUTTON_X1));
    check(event->data.pointer.changed_buttons == OS_POINTER_BUTTON_LEFT);
}

int main() {
    KernelInputEventQueue queue;
    OsInputEvent event;
    input_event_queue_init(&queue);

    check(input_event_queue_capacity(&queue) == INPUT_EVENT_QUEUE_CAPACITY);
    check(input_event_queue_count(&queue) == 0);
    check(input_event_queue_is_empty(&queue) == 1);
    check(input_event_queue_is_full(&queue) == 0);
    check(input_event_queue_delivered_count(&queue) == 0);
    check(input_event_queue_dropped_count(&queue) == 0);
    check(input_event_queue_pop(&queue, &event) == 0);

    event = make_pointer_event(7u);
    check(input_event_queue_push(&queue, &event) == 1);
    check(input_event_queue_pop(&queue, &event) == 1);
    expect_pointer_event(&event, 7u);
    check(input_event_queue_is_empty(&queue) == 1);

    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY; i++) {
        event = make_key_event(i);
        check(input_event_queue_push(&queue, &event) == 1);
    }
    check(input_event_queue_count(&queue) == INPUT_EVENT_QUEUE_CAPACITY);
    check(input_event_queue_is_empty(&queue) == 0);
    check(input_event_queue_is_full(&queue) == 1);

    event = make_key_event(999u);
    check(input_event_queue_push(&queue, &event) == 0);
    check(input_event_queue_count(&queue) == INPUT_EVENT_QUEUE_CAPACITY);
    check(input_event_queue_dropped_count(&queue) == 0);

    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, i);
    }
    check(input_event_queue_is_empty(&queue) == 1);
    check(input_event_queue_delivered_count(&queue) == INPUT_EVENT_QUEUE_CAPACITY + 1u);

    for (uint32_t i = 0; i < 10; i++) {
        event = make_key_event(100u + i);
        check(input_event_queue_push(&queue, &event) == 1);
    }
    for (uint32_t i = 0; i < 7; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, 100u + i);
    }
    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY - 3u; i++) {
        event = make_key_event(200u + i);
        check(input_event_queue_push(&queue, &event) == 1);
    }
    check(input_event_queue_is_full(&queue) == 1);

    for (uint32_t i = 0; i < 3; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, 107u + i);
    }
    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY - 3u; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, 200u + i);
    }
    check(input_event_queue_is_empty(&queue) == 1);
    check(input_event_queue_pop(0, &event) == 0);
    check(input_event_queue_push(&queue, 0) == 0);

    input_event_queue_init(&queue);
    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY; i++) {
        event = make_key_event(300u + i);
        check(input_event_queue_push_drop_oldest(&queue, &event) == 1);
    }
    check(input_event_queue_is_full(&queue) == 1);
    check(input_event_queue_dropped_count(&queue) == 0);

    for (uint32_t i = 0; i < 5; i++) {
        event = make_key_event(400u + i);
        check(input_event_queue_push_drop_oldest(&queue, &event) == 1);
    }
    check(input_event_queue_count(&queue) == INPUT_EVENT_QUEUE_CAPACITY);
    check(input_event_queue_dropped_count(&queue) == 5);

    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY - 5u; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, 305u + i);
    }
    for (uint32_t i = 0; i < 5; i++) {
        check(input_event_queue_pop(&queue, &event) == 1);
        expect_key_event(&event, 400u + i);
    }
    check(input_event_queue_is_empty(&queue) == 1);
    check(input_event_queue_push_drop_oldest(&queue, 0) == 0);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_input_queue_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "input_event_queue_test.cpp"
        binary_path = temp_path / "input_event_queue_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/input/input_event_queue.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("input event queue test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
