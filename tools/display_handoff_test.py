#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(path: str, markers: tuple[str, ...]) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    missing = [marker for marker in markers if marker not in text]
    if missing:
        raise RuntimeError(f"{path}: missing {missing}")


def main() -> int:
    require("include/os64/display_types.h", (
        "OS_DISPLAY_SESSION_CONSOLE_ACTIVE",
        "OS_DISPLAY_SESSION_GUI_ACTIVE",
        "OsDisplaySessionInfo",
        "sizeof(OsDisplaySessionInfo) == 40",
    ))
    require("kernel/graphics/display_owner.cpp", (
        "display_session_begin(",
        "display_session_commit(",
        "display_session_begin_release(",
        "display_session_begin_recovery(",
        "display_session_present_allowed(",
    ))
    require("kernel/syscall/sdk_syscalls.cpp", (
        "dispatch_display_session_acquire",
        "kernel_handle_restrict_rights",
        "OS_SURFACE_TRANSFER_RIGHTS",
        "input_events_discard_all",
        "dispatch_display_session_release",
    ))
    require("drivers/display/terminal/terminal.cpp", (
        "display_session_terminal_scanout_allowed",
        "int Terminal::redraw()",
        "display_session_process_cleanup",
    ))
    require("user/programs/windowd_c.c", (
        "acquire_gui_session",
        "ensure_gui_session",
        "release_gui_session",
        "window_compositor_compose_underlay",
        "underlay=read-only",
    ))
    require("kernel/input/input_events.cpp", (
        "gui_session_active",
        "Process* focused = gui_active ? 0 : process_focused()",
    ))
    kernel_syscalls = (ROOT / "include/kernel/syscall64.h").read_text()
    sdk_syscalls = (ROOT / "user/sdk/src/internal.h").read_text()
    for name, number in (
        ("DISPLAY_SESSION_ACQUIRE", 86),
        ("DISPLAY_SESSION_RELEASE", 87),
        ("DISPLAY_SESSION_GET_INFO", 88),
    ):
        if f"SYS_{name} {number}" not in kernel_syscalls:
            raise RuntimeError(f"kernel syscall mismatch: {name}")
        if f"OS_SYS_{name} = {number}" not in sdk_syscalls:
            raise RuntimeError(f"SDK syscall mismatch: {name}")
    print("display session and console handoff contract test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
