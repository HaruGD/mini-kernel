#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise RuntimeError(f"missing {label}: {needle}")


def main() -> int:
    numbers = (ROOT / "include/os64/syscall_numbers.h").read_text()
    internal = (ROOT / "user/sdk/src/internal.h").read_text()
    public = (ROOT / "include/os64/thread_types.h").read_text()
    sdk = (ROOT / "user/sdk/src/thread.c").read_text()
    process = (ROOT / "include/kernel/process.h").read_text()
    scheduler = (ROOT / "kernel/process/process64.cpp").read_text()

    for name, number in (
        ("THREAD_CREATE", 89), ("THREAD_SELF", 90),
        ("THREAD_EXIT", 91), ("THREAD_JOIN", 92),
        ("THREAD_SET_AFFINITY", 107),
    ):
        require(numbers, f"#define SYS_{name} {number}",
                f"shared kernel syscall {name}")
        require(numbers, f"#define OS_SYS_{name} {number}",
                f"shared SDK syscall {name}")

    require(internal, '#include "os64/syscall_numbers.h"',
            "SDK generated-number include")

    require(public, "sizeof(OsThreadIdentity) == 8", "thread identity ABI")
    require(public, "sizeof(OsThreadCreateRequest) == 40", "create request ABI")
    require(public, "OS64_THREAD_ABI_VERSION 2u", "thread ABI version")
    require(public, "sizeof(OsThreadInfo) == 112", "thread info ABI")
    require(sdk, "os_thread_return_trampoline", "return trampoline")
    require(sdk, "os_thread_set_affinity", "affinity SDK wrapper")
    require(process, "ThreadContext main_thread_context", "main context storage")
    require(scheduler, "Thread* sched_queue", "thread ready queue")
    require(scheduler, "thread_identity_matches", "generation validation")
    print("thread ABI and main-thread extraction contract test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
