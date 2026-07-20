#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    main_source = (ROOT / "kernel/core/kernel64_main.cpp").read_text()
    idle = main_source[main_source.rfind("while (1)"):]
    ready = idle.find("continue_ready_processes(0)")
    halt = idle.find('"sti; hlt"')
    if ready < 0 or halt < 0 or ready > halt:
        raise RuntimeError("kernel idle context does not schedule before halt")

    shell = (ROOT / "kernel/shell/ksh64.cpp").read_text()
    if 'strcmp64(cmd, "drive")' in shell or "udrive_c.elf" in shell:
        raise RuntimeError("transitional drive shell path remains")
    if (ROOT / "user/programs/udrive_c.c").exists():
        raise RuntimeError("transitional drive helper remains")

    for script in ("gui_app_smoke.py", "window_input_smoke.py",
                   "window_single_smoke.py", "window_multi_smoke.py"):
        text = (ROOT / "tools" / script).read_text()
        if "udrive_c.elf" in text or "run uyield_c.elf" in text or \
                "drive_until" in text or "drive_guest" in text:
            raise RuntimeError(f"{script} still drives guest scheduling")

    service_client = (ROOT / "user/programs/usvcctl_c.c").read_text()
    if "SERVICE_MANAGER_REPLY_TIMEOUT_TICKS" not in service_client or \
            "os_msg_wait_timeout" not in service_client:
        raise RuntimeError("service client reply wait is not bounded")
    process_core = (ROOT / "kernel/core/kernel64_process.cpp").read_text()
    if "wait could not enqueue" in process_core or \
            "if (!process_wait_is_pending(process))" not in process_core or \
            "scheduler_enqueue_thread(thread);" not in process_core or \
            "The thread table is authoritative" not in \
            (ROOT / "kernel/process/process64.cpp").read_text():
        raise RuntimeError("wait-arm/wakeup race closure is missing")
    print("drive-free idle scheduler contract test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
