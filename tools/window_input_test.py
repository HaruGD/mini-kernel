#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>

#include "window_input_router.h"
#include <os64/result.h>
#include <os64/window_types.h>

static int failures;

static void check(int condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

int main(void) {
    check(sizeof(OsWindowInputForward) == 72, "input forward ABI");
    check(sizeof(OsWindowEvent) == 80, "window event ABI");

    WindowInputRouter router;
    window_input_router_init(&router);
    OsProcessIdentity a = {10, 2};
    OsProcessIdentity b = {11, 3};
    WindowFocusChange change;

    window_input_router_focus(&router, a, 1, 4, &change);
    check(change.focus_out.event_sequence == 0 &&
          change.focus_in.event_sequence == 1 &&
          change.focus_in.window_id == 1,
          "first focus emits only focus-in");
    check(window_input_router_is_focused(&router, a, 1, 4),
          "first focus state");

    window_input_router_focus(&router, a, 1, 4, &change);
    check(change.focus_out.event_sequence == 0 &&
          change.focus_in.event_sequence == 0 && router.event_sequence == 1,
          "idempotent focus emits nothing");

    check(window_input_router_accept_input(&router, 1) == OS_SUCCESS,
          "first input sequence");
    check(window_input_router_accept_input(&router, 1) == OS_ERR_INVALID_ARGUMENT,
          "duplicate input rejected");
    check(window_input_router_accept_input(&router, 0) == OS_ERR_INVALID_ARGUMENT,
          "zero input rejected");
    check(window_input_router_accept_input(&router, 3) == OS_SUCCESS,
          "monotonic input accepts bounded drop gap");

    window_input_router_focus(&router, b, 2, 7, &change);
    check(change.focus_out.event_sequence == 2 &&
          change.focus_out.owner.pid == a.pid &&
          change.focus_in.event_sequence == 3 &&
          change.focus_in.owner.pid == b.pid,
          "focus-out precedes focus-in");
    check(!window_input_router_is_focused(&router, b, 2, 6),
          "stale window generation rejected");
    check(!window_input_router_is_focused(&router,
                                          (OsProcessIdentity){11, 4}, 2, 7),
          "reused pid generation rejected");

    uint32_t key_sequence = window_input_router_next_event(&router);
    check(key_sequence == 4, "key event sequence shares ordered stream");
    window_input_router_clear(&router, &change);
    check(change.focus_out.event_sequence == 5 &&
          change.focus_in.event_sequence == 0 &&
          router.focused_window_id == 0,
          "clear emits focus-out and no replacement");

    window_input_router_reset(&router);
    check(router.input_sequence == 0 && router.event_sequence == 0,
          "restart resets transport sequences");
    if (failures != 0) return 1;
    puts("window input router tests OK");
    return 0;
}
'''


def require_sources() -> None:
    windowd = (ROOT / "user/programs/windowd_c.c").read_text(encoding="utf-8")
    inputd = (ROOT / "user/programs/inputd_c.c").read_text(encoding="utf-8")
    syscall = (ROOT / "kernel/syscall/sdk_syscalls.cpp").read_text(encoding="utf-8")
    events = (ROOT / "kernel/input/input_events.cpp").read_text(encoding="utf-8")
    required_windowd = (
        'os_service_find_owner_identity("input"',
        "OS_WINDOW_EVENT_FOCUS_OUT",
        "OS_WINDOW_EVENT_FOCUS_IN",
        "reconcile_invalid_focus",
        "os_process_identity_alive",
        "window_input_router_accept_input",
    )
    required_inputd = (
        "os_input_wait_timeout",
        "OS_WINDOW_INPUT_EVENT",
        "os_msg_v2_send_to_identity",
        "OS_ERR_QUEUE_FULL",
        'os_service_find_owner_identity("window"',
    )
    required_kernel = (
        "process_is_input_authority",
        'service_find_owner_identity("input"',
        "input_events_pop(&event)",
    )
    if any(marker not in windowd for marker in required_windowd):
        raise RuntimeError("windowd focus integration is incomplete")
    if any(marker not in inputd for marker in required_inputd):
        raise RuntimeError("inputd forwarding/reconnect integration is incomplete")
    if any(marker not in syscall for marker in required_kernel):
        raise RuntimeError("registered raw-input authority is incomplete")
    if 'process_wait_signal(input_service, PROCESS_WAIT_INPUT' not in events:
        raise RuntimeError("input service blocking wakeup is missing")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_window_input_") as directory:
        source = Path(directory) / "window_input_test.c"
        binary = Path(directory) / "window_input_test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user/sdk/include"),
            "-I", str(ROOT / "user/programs/windowd"),
            str(source),
            str(ROOT / "user/programs/windowd/window_input_router.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    require_sources()
    print("window input service/authority source checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
