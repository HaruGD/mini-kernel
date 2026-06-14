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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-log", default="serial_uefi_smoke.log")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("commands", nargs="*", default=[
        "bootinfo",
        "memstat",
        "drivers",
        "drvcheck hello.drv",
        "run hello.drv",
        "drvload hello.drv",
        "drvcheck provider.drv",
        "drvload provider.drv",
        "drvcheck consumer.drv",
        "drvload consumer.drv",
        "drvcheck hello_c.drv",
        "drvload hello_c.drv",
        "drivers",
        "mounts",
        "ls",
        "ls /mem",
    ])
    args = parser.parse_args()

    serial_log = Path(args.serial_log)
    serial_log.unlink(missing_ok=True)
    esp_image = Path("bin/uefi_esp.smoke.img")
    root_image = Path("bin/os64.smoke.img")
    esp_image.unlink(missing_ok=True)
    root_image.unlink(missing_ok=True)
    shutil.copyfile("bin/uefi_esp.img", esp_image)
    shutil.copyfile("bin/os64.bin", root_image)

    qemu = [
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", "if=pflash,format=raw,file=./bin/OVMF_VARS_4M.fd",
        "-drive", f"if=none,id=uefi_esp,format=raw,file={esp_image}",
        "-device", "virtio-blk-pci,drive=uefi_esp,bootindex=1",
        "-drive", f"format=raw,file={root_image},if=ide,index=0",
        "-serial", f"file:{serial_log}",
        "-monitor", "stdio",
        "-display", "none",
    ]

    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    deadline = time.time() + args.timeout
    try:
        time.sleep(10.0)
        for command in args.commands:
            send_shell_command(proc, command)
            time.sleep(1.2)
        time.sleep(2.0)
        send_monitor_line(proc, "quit")
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    finally:
        if proc.stdin is not None:
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
        esp_image.unlink(missing_ok=True)
        root_image.unlink(missing_ok=True)

    if time.time() > deadline and proc.poll() is None:
        proc.kill()

    log_text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    checks = [
        "Boot path: UEFI",
        "OS64>",
        "BootInfo magic:",
        "Reserved range count:",
        "Reserved[0x00000000] kernel",
        "pmm=used",
        "=== DRIVERS ===",
        "ata0 kind=block state=ready",
        "keyboard kind=input state=ready",
        "DRV check OK",
        "DRV load OK",
        "name=hello",
        "sections=0x00000001 imports=0x00000001",
        "DRV packages are kernel drivers. Use: drvload hello.drv",
        "[drv] hello.drv driver_entry()",
        "hello kind=module state=ready",
        "[drv] provider.drv driver_entry()",
        "[drv] consumer.drv driver_entry()",
        "[drv] provider.drv provider_ping()",
        "[drv] hello_c.drv driver_entry()",
        "provider kind=module state=ready",
        "consumer kind=module state=ready",
        "hello_c kind=module state=ready",
        "fat32 kind=fs state=ready",
        "kernel64.bin",
        "ushell_c.elf",
        "hello.drv",
        "provider.drv",
        "consumer.drv",
        "hello_c.drv",
        "PMM total pages:",
        "=== VFS MOUNTS ===",
    ]
    missing = [check for check in checks if check not in log_text]
    if missing:
        print("UEFI smoke missing:")
        for item in missing:
            print(item)
        return 1

    print("UEFI smoke OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
