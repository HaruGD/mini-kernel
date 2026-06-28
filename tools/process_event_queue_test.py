#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/process64.h"

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
    event.timestamp_ticks = sequence;
    event.data.key.type = OS_KEY_EVENT_DOWN;
    event.data.key.keycode = sequence;
    event.data.key.modifiers = 0;
    event.data.key.character = 'a';
    return event;
}

int main() {
    Process process;
    OsInputEvent event;

    process_clear(&process);
    check(process_event_queue_count(&process) == 0);
    check(process_event_queue_delivered_count(&process) == 0);
    check(process_event_queue_dropped_count(&process) == 0);

    event = make_key_event(1);
    check(process_event_queue_push(&process, &event) == 1);
    check(process_event_queue_count(&process) == 1);
    check(process_event_queue_pop(&process, &event) == 1);
    check(event.type == OS_INPUT_EVENT_KEY);
    check(event.data.key.keycode == 1);
    check(process_event_queue_count(&process) == 0);
    check(process_event_queue_delivered_count(&process) == 1);

    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY + 3u; i++) {
        event = make_key_event(100u + i);
        check(process_event_queue_push(&process, &event) == 1);
    }
    check(process_event_queue_count(&process) == INPUT_EVENT_QUEUE_CAPACITY);
    check(process_event_queue_dropped_count(&process) == 3);

    process.pid = 42;
    process.parent_pid = 7;
    process.active = 1;
    process_mark_returned(&process, PROCESS_TERM_EXIT, 0);
    check(process.state == PROCESS_STATE_RETURNED);
    check(process.active == 0);
    check(process_event_queue_count(&process) == 0);
    check(process_event_queue_delivered_count(&process) == 0);
    check(process_event_queue_dropped_count(&process) == 0);

    event = make_key_event(200);
    check(process_event_queue_push(&process, &event) == 1);
    process.active = 1;
    process_mark_failed(&process, PROCESS_TERM_KILLED, 9);
    check(process.state == PROCESS_STATE_FAILED);
    check(process.status_code == 9);
    check(process_event_queue_count(&process) == 0);

    event = make_key_event(300);
    check(process_event_queue_push(&process, &event) == 1);
    process_clear(&process);
    check(process.pid == 0);
    check(process_event_queue_count(&process) == 0);
    check(process_event_queue_delivered_count(&process) == 0);
    check(process_event_queue_dropped_count(&process) == 0);

    check(process_event_queue_push(0, &event) == 0);
    check(process_event_queue_pop(0, &event) == 0);
    check(process_event_queue_count(0) == 0);
    check(process_event_queue_delivered_count(0) == 0);
    check(process_event_queue_dropped_count(0) == 0);

    return failures == 0 ? 0 : 1;
}
"""


STUB_SOURCE = r"""
#include <stdint.h>

uint32_t vfs_close_all_for_owner(uint32_t) {
    return 0;
}

void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_process_events_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "process_event_queue_test.cpp"
        stub_path = temp_path / "process_event_queue_stubs.cpp"
        binary_path = temp_path / "process_event_queue_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        stub_path.write_text(textwrap.dedent(STUB_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/input/input_event_queue.cpp"),
            str(REPO_ROOT / "kernel/process/process64.cpp"),
            str(source_path),
            str(stub_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("process event queue test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
