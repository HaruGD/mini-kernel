#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "drivers/keyboard.h"
#include "drivers/pit.h"

PIT pit;

PIT::PIT() : tick(0) {}
void PIT::init() {}
void PIT::init(uint32_t) {}
void PIT::handle() {}
uint32_t PIT::get_tick() { return 0; }
uint64_t PIT::get_tick64() const { return 0; }
uint32_t PIT::get_frequency() const { return 100; }
uint32_t PIT::ticks_to_ms(uint32_t ticks) const { return ticks * 10u; }

extern "C" void shell_recall_history(int) {}
extern "C" void shell_input(char) {}
extern "C" int user_input_active64() { return 0; }
extern "C" void keyboard_deliver_char64(char) {}
extern "C" void input_events_init() {}
extern "C" int input_events_push(const OsInputEvent*) { return 1; }

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static void expect_key(const OsInputEvent* event,
                       uint32_t type,
                       uint32_t keycode,
                       uint32_t modifiers,
                       uint32_t character,
                       uint64_t timestamp) {
    check(event->type == OS_INPUT_EVENT_KEY);
    check(event->size == sizeof(OsInputEvent));
    check(event->timestamp_ticks == timestamp);
    check(event->data.key.type == type);
    check(event->data.key.keycode == keycode);
    check(event->data.key.modifiers == modifiers);
    check(event->data.key.character == character);
}

int main() {
    KeyboardDriver keyboard;
    OsInputEvent event;

    check(keyboard.process_scan_code(0x2A, 10, &event) == true);
    expect_key(&event, OS_KEY_EVENT_DOWN, 0x2A, OS_KEY_MOD_SHIFT, 0, 10);

    check(keyboard.process_scan_code(0x2C, 11, &event) == true);
    expect_key(&event, OS_KEY_EVENT_DOWN, 0x2C, OS_KEY_MOD_SHIFT, 'Z', 11);

    check(keyboard.process_scan_code(0xAC, 12, &event) == true);
    expect_key(&event, OS_KEY_EVENT_UP, 0x2C, OS_KEY_MOD_SHIFT, 0, 12);

    check(keyboard.process_scan_code(0xAA, 13, &event) == true);
    expect_key(&event, OS_KEY_EVENT_UP, 0x2A, 0, 0, 13);

    check(keyboard.process_scan_code(0x3A, 14, &event) == true);
    expect_key(&event, OS_KEY_EVENT_DOWN, 0x3A, OS_KEY_MOD_CAPS_LOCK, 0, 14);

    check(keyboard.process_scan_code(0x1E, 15, &event) == true);
    expect_key(&event, OS_KEY_EVENT_DOWN, 0x1E, OS_KEY_MOD_CAPS_LOCK, 'A', 15);

    check(keyboard.process_scan_code(0x9E, 16, &event) == true);
    expect_key(&event, OS_KEY_EVENT_UP, 0x1E, OS_KEY_MOD_CAPS_LOCK, 0, 16);

    check(keyboard.process_scan_code(0xE0, 17, &event) == false);
    check(keyboard.process_scan_code(0x48, 18, &event) == true);
    expect_key(&event, OS_KEY_EVENT_DOWN, OS_KEY_UP, OS_KEY_MOD_CAPS_LOCK, 0, 18);

    check(keyboard.process_scan_code(0xE0, 19, &event) == false);
    check(keyboard.process_scan_code(0xC8, 20, &event) == true);
    expect_key(&event, OS_KEY_EVENT_UP, OS_KEY_UP, OS_KEY_MOD_CAPS_LOCK, 0, 20);

    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_keyboard_event_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "keyboard_event_translation_test.cpp"
        binary_path = temp_path / "keyboard_event_translation_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "drivers/builtin/keyboard/keyboard.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("keyboard event translation test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
