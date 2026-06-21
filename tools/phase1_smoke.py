#!/usr/bin/env python3
import shutil
import subprocess
import time
from pathlib import Path


KEY_MAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
}


def monitor_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()


def shell_command(process: subprocess.Popen, command: str) -> None:
    for character in command:
        monitor_line(process, f"sendkey {KEY_MAP.get(character, character)}")
        time.sleep(0.025)
    monitor_line(process, "sendkey ret")


def run_session(
    name: str,
    commands: tuple[str, ...],
    diagnostic: bool = True,
) -> str:
    serial_log = Path(f"logs/serial_phase1_{name}.log")
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    serial_log.unlink(missing_ok=True)
    esp_image = Path(f"bin/uefi_diag_{name}.smoke.img")
    vars_image = Path(f"bin/OVMF_VARS_4M.phase1_{name}.fd")
    source_image = "bin/uefi_diag_esp.img" if diagnostic else "bin/uefi_esp.img"
    shutil.copyfile(source_image, esp_image)
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_image)

    qemu = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", "512M",
        "-cpu", "max",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_image}",
        "-drive", f"if=none,id=esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-boot", "menu=off",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-no-reboot",
    ]
    process = subprocess.Popen(
        qemu,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )

    try:
        time.sleep(8.0)
        for command in commands:
            shell_command(process, command)
            time.sleep(1.0)
        time.sleep(1.0)
    finally:
        process.kill()
        process.wait(timeout=3.0)
        if process.stdin is not None:
            process.stdin.close()
        esp_image.unlink(missing_ok=True)
        vars_image.unlink(missing_ok=True)

    return serial_log.read_text(errors="replace")


def require_all(text: str, required: list[str], label: str) -> list[str]:
    return [f"{label}: {item}" for item in required if item not in text]


def command_interval(text: str, command: str, next_command: str | None) -> str:
    start = text.find(f"OS64> {command}")
    if start < 0:
        return ""
    if next_command is None:
        return text[start:]
    end = text.find(f"OS64> {next_command}", start + 1)
    return text[start:] if end < 0 else text[start:end]


def require_clean_recent_log(text: str, label: str) -> list[str]:
    start_marker = "Recent kernel log (last 4 KiB):"
    end_marker = "End recent kernel log."
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        return [f"{label}: recent kernel log boundaries not found"]
    recent = text[start + len(start_marker):end]
    if "OS64 KERNEL PANIC" in recent or "[FATAL][panic]" in recent:
        return [f"{label}: panic output was captured into the recent log"]
    return []


def main() -> int:
    acpi_commands = (
        "debugfault acpi_rsdp_checksum",
        "acpi",
        "intctl",
        "debugfault acpi_madt_entry_len",
        "acpi",
        "intctl",
        "debugfault acpi_no_ioapic",
        "acpi",
        "intctl",
    )
    acpi_text = run_session("acpi_faults", acpi_commands)
    fault_text = run_session(
        "faults",
        (
            "acpi",
            "intctl",
            "klog stats",
            "run ufault_c.elf",
            "run ugpfault_c.elf",
            "klog clear",
            "pagefault",
        ),
    )
    gp_text = run_session("gp", ("debugfault gp",))
    panic_text = run_session("panic", ("panic test",))
    normal_text = run_session("normal_reject", ("debugfault gp",), diagnostic=False)

    Path("logs/serial_phase1_smoke.log").write_text(
        "=== ACPI fault session ===\n" + acpi_text +
        "\n=== fault session ===\n" + fault_text +
        "\n=== GP session ===\n" + gp_text +
        "\n=== panic session ===\n" + panic_text +
        "\n=== normal rejection session ===\n" + normal_text,
        errors="replace",
    )

    missing = require_all(acpi_text, [
        "debugfault result=ok case=acpi_rsdp_checksum",
        "debugfault result=ok case=acpi_madt_entry_len",
        "debugfault result=ok case=acpi_no_ioapic",
    ], "ACPI fault session")
    for index, command in enumerate(acpi_commands):
        if not command.startswith("debugfault"):
            continue
        next_command = acpi_commands[index + 1] if index + 1 < len(acpi_commands) else None
        interval = command_interval(acpi_text, command, next_command)
        if not interval:
            missing.append(f"ACPI fault session: command not observed: {command}")
        elif "KERNEL PANIC" in interval:
            missing.append(f"ACPI fault session: panic during {command}")

    if acpi_text.count("ready=0x00000000") < 3:
        missing.append("ACPI fault session: corrupted ACPI was not left unready")
    if acpi_text.count("mode=PIC") < 3:
        missing.append("ACPI fault session: PIC fallback was not retained")

    missing += require_all(fault_text, [
        "Diagnostic mode: 0x00000001",
        "Driver autoloaded: 0x00000000",
        "ACPI ready: 0x00000001",
        "Interrupt controller: APIC ready=0x00000001",
        "=== ACPI ===",
        "mode=APIC",
        "pic_spurious_count=",
        "=== KLOG ===",
        "=== PAGE FAULT ===",
        "state=failed term=page_fault",
        "=== GENERAL PROTECTION FAULT ===",
        "state=failed term=gp_fault",
        "Kernel log cleared.",
        "OS64 KERNEL PANIC",
        "Reason: kernel page fault",
    ], "fault session")
    missing += require_all(gp_text, [
        "OS64 KERNEL PANIC",
        "Reason: kernel general protection fault",
    ], "GP session")
    missing += require_all(panic_text, [
        "OS64 KERNEL PANIC",
        "Reason: manual panic test",
        "Stack trace:",
        "Recent kernel log (last 4 KiB):",
        "System halted. Inspect serial output and klog.",
    ], "panic session")
    missing += require_all(normal_text, [
        "Diagnostic mode: 0x00000000",
        "debugfault is only available in diagnostic boot mode",
    ], "normal rejection session")
    if "KERNEL PANIC" in normal_text:
        missing.append("normal rejection session: debugfault caused a panic")
    missing += require_clean_recent_log(fault_text, "fault session")
    missing += require_clean_recent_log(gp_text, "GP session")
    missing += require_clean_recent_log(panic_text, "panic session")

    user_start = fault_text.find("Running user program: ufault_c.elf")
    user_gp_start = fault_text.find("Running user program: ugpfault_c.elf", user_start)
    kernel_fault_start = fault_text.find("OS64> pagefault", user_gp_start)
    if user_start < 0 or user_gp_start < 0 or kernel_fault_start < 0:
        missing.append("fault session: could not isolate user fault interval")
    else:
        user_page_interval = fault_text[user_start:user_gp_start]
        if "KERNEL PANIC" in user_page_interval:
            missing.append("fault session: user page fault caused a kernel panic")

        user_gp_interval = fault_text[user_gp_start:kernel_fault_start]
        if "KERNEL PANIC" in user_gp_interval:
            missing.append("fault session: user GP fault caused a kernel panic")
        if "This line should never print." in user_gp_interval:
            missing.append("fault session: user GP fault instruction returned")

        kernel_interval = fault_text[kernel_fault_start:]
        if "state=failed term=page_fault" in kernel_interval:
            missing.append("fault session: kernel page fault used the user fault path")

    if missing:
        print("Phase 1 smoke failures:")
        for item in missing:
            print(item)
        return 1

    print("Phase 1 smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
