#!/usr/bin/env python3
import argparse
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
for _ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    KEY_MAP[_ch] = "shift-" + _ch.lower()


def send_monitor_line(proc: subprocess.Popen, line: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write((line + "\n").encode("ascii"))
    proc.stdin.flush()


def send_shell_command(proc: subprocess.Popen, command: str) -> None:
    for ch in command:
        key = KEY_MAP.get(ch, ch)
        send_monitor_line(proc, f"sendkey {key}")
        time.sleep(0.04)
    send_monitor_line(proc, "sendkey ret")


def wait_for_serial_contains(path: Path, needle: str, timeout: float) -> None:
    deadline = time.time() + timeout
    encoded = needle.encode("ascii")
    while time.time() < deadline:
        try:
            if encoded in path.read_bytes():
                return
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for serial output {needle!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-log", default="logs/serial_uefi_smoke.log")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("commands", nargs="*", default=[
        "bootinfo",
        "memstat",
        "input",
        "drivers",
        "bindings",
        "irqhooks",
        "pci",
        "drvcheck hello.drv",
        "run hello.drv",
        "drvload hello.drv",
        "drvunload hello",
        "drvload hello.drv",
        "drvreload hello.drv",
        "drvcheck provider.drv",
        "drvload provider.drv",
        "drvcheck consumer.drv",
        "drvload consumer.drv",
        "drvcheck hello_c.drv",
        "drvload hello_c.drv",
        "drvcheck gop_demo_c.drv",
        "drvload gop_demo_c.drv",
        "drvcheck provider_c.drv",
        "drvload provider_c.drv",
        "drvcheck consumer_c.drv",
        "drvload consumer_c.drv",
        "drivers",
        "drvunload provider_c",
        "drvlast",
        "drvinfo consumer_c.drv",
        "bindings",
        "irqhooks",
        "mounts",
        "ls",
        "ls /mem",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
        "run uvers_c.elf",
    ])
    args = parser.parse_args()

    serial_log = Path(args.serial_log)
    serial_log.parent.mkdir(parents=True, exist_ok=True)
    serial_log.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_esp.smoke.img")
    esp_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)

    qemu = [
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", "if=pflash,format=raw,file=./bin/OVMF_VARS_4M.fd",
        "-drive", f"if=none,id=uefi_esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=uefi_esp,bootindex=1",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-display", "none",
    ]

    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    deadline = time.time() + args.timeout
    try:
        wait_for_serial_contains(serial_log, "OS64>", args.timeout)
        for command in args.commands:
            send_shell_command(proc, command)
            time.sleep(1.2)
        time.sleep(2.0)
        send_monitor_line(proc, "quit")
        proc.wait(timeout=5)
    except (subprocess.TimeoutExpired, TimeoutError):
        proc.kill()
        proc.wait()
    finally:
        if proc.stdin is not None:
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
        esp_image.unlink(missing_ok=True)

    if time.time() > deadline and proc.poll() is None:
        proc.kill()

    log_text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    checks = [
        "Boot path: UEFI",
        "Driver autoloaded:",
        "OS64>",
        "BootInfo magic:",
        "Ramdisk:",
        "Ramdisk addr:",
        "Reserved range count:",
        "Reserved[0x00000000] kernel",
        "ramdisk",
        "pmm=used",
        "Root source: ramdisk",
        "=== DRIVERS ===",
        "=== PCI ===",
        "count=",
        "ata0 kind=block state=ready",
        "keyboard kind=input state=ready",
        "DRV check OK",
        "DRV load OK",
        "name=hello",
        "sections=0x00000001 imports=0x00000001",
        "DRV packages are kernel drivers. Use: drvload hello.drv",
        "[drv] hello.drv driver_entry()",
        "DRV unload OK result=ok name=hello",
        "DRV reload OK result=ok file=hello.drv",
        "hello kind=module state=ready",
        "[drv] provider.drv driver_entry()",
        "[drv] consumer.drv driver_entry()",
        "[drv] provider.drv provider_ping()",
        "[drv] hello_c.drv driver_entry()",
        "[drv] provider_c.drv driver_entry()",
        "[drv] consumer_c.drv driver_entry()",
        "[drv] provider_c.drv provider_ping()",
        "[drv] pci_probe_c.drv driver_entry()",
        "[drv] pci_probe_c.drv pci_read_config32(0,0,0,0)",
        "[drv] pci_probe_c.drv bound Bochs VGA",
        "[drv] gop_demo_c.drv driver_entry()",
        "[drv] gop_demo_c.drv GOP draw OK",
        "[drv] irq_timer_c.drv driver_entry()",
        "[drv] hello_cpp.drv driver_entry()",
        "[drv] hello_cpp.drv C++ method call OK",
        "provider kind=module state=ready",
        "consumer kind=module state=ready",
        "hello_c kind=module state=ready",
        "hello_cpp kind=module state=ready",
        "gop_demo_c kind=module state=ready",
        "irq_timer_c kind=module state=ready",
        "provider_c kind=module state=ready",
        "consumer_c kind=module state=ready",
        "pci_probe_c kind=module state=ready",
        "DRV unload FAILED result=unload_denied name=provider_c",
        "=== DRV LAST ===",
        "result=unload_denied",
        "error_stage=unload module=provider_c name=consumer_c",
        "=== DRV INFO ===",
        "deps=0x00000001",
        "dep[0x00000000] provider_c",
        "import[0x00000001] provider_c.provider_ping",
        "=== DRIVER BINDINGS ===",
        "pci_probe_c pci",
        "vendor=0x00001234 device=0x00001111",
        "=== IRQ HOOKS ===",
        "irq=0x00000000 driver=irq_timer_c calls=0x",
        "fat32 kind=fs state=ready",
        "kernel64.bin",
        "ushell_c.elf",
        "hello.drv",
        "provider.drv",
        "consumer.drv",
        "gop_demo_c.drv",
        "hello_c.drv",
        "irq_timer_c.drv",
        "provider_c.drv",
        "consumer_c.drv",
        "PMM total pages:",
        "=== INPUT ===",
        "delivered=",
        "dropped=",
        "=== VFS MOUNTS ===",
    ]
    missing = [check for check in checks if check not in log_text]
    if missing:
        print("UEFI smoke missing:")
        for item in missing:
            print(item)
        return 1

    forbidden = [
        "Process table is full",
    ]
    present = [check for check in forbidden if check in log_text]
    if present:
        print("UEFI smoke unexpected:")
        for item in present:
            print(item)
        return 1

    print("UEFI smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
