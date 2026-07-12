#!/usr/bin/env python3
import json
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


KEY_MAP = {" ": "spc", "/": "slash", ".": "dot", "_": "shift-minus"}


def send_line(process: subprocess.Popen, line: str) -> None:
    assert process.stdin is not None
    process.stdin.write((line + "\n").encode("ascii"))
    process.stdin.flush()


def send_command(process: subprocess.Popen, command: str) -> None:
    for character in command:
        send_line(process, "sendkey " + KEY_MAP.get(character, character))
        time.sleep(0.025)
    send_line(process, "sendkey ret")


def wait_for(path: Path, text: str, timeout: float) -> None:
    deadline = time.time() + timeout
    encoded = text.encode("ascii")
    while time.time() < deadline:
        if path.exists() and encoded in path.read_bytes():
            return
        time.sleep(0.1)
    raise RuntimeError(f"timed out waiting for {text!r}")


def boot(repo: Path, image: Path, label: str) -> str:
    log = repo / "logs" / f"boot_driver_{label}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    log.unlink(missing_ok=True)
    vars_file = repo / "bin" / f"OVMF_VARS_{label}.fd"
    shutil.copyfile(repo / "bin/OVMF_VARS_4M.fd", vars_file)
    process = subprocess.Popen([
        "qemu-system-x86_64",
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={vars_file}",
        "-drive", f"if=none,id=esp,format=raw,file={image}",
        "-device", "virtio-blk-pci,drive=esp,bootindex=1",
        "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
    ], cwd=repo, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        wait_for(log, "OS64>", 35.0)
        send_command(process, "bootinfo")
        time.sleep(1.0)
        send_command(process, "drivers")
        time.sleep(1.5)
        send_line(process, "quit")
        process.wait(timeout=5)
    except Exception:
        process.kill()
        process.wait()
        raise
    finally:
        if process.stdin is not None:
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
        vars_file.unlink(missing_ok=True)
    return log.read_text(errors="replace")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def copy_worktree(source: Path, destination: Path) -> None:
    files = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=source, text=True,
    ).splitlines()
    for relative in files:
        src = source / relative
        if not src.is_file():
            continue
        dst = destination / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def set_boot_policy(repo: Path, names: set[str]) -> None:
    path = repo / "config/drivers.json"
    policy = json.loads(path.read_text(encoding="utf-8"))
    for entry in policy["drivers"]:
        if entry["name"] in names:
            entry["artifact"] = "drv"
            entry["load_stage"] = "boot"
            entry["load_policy"] = "automatic"
        elif entry["artifact"] == "drv" and entry["load_stage"] == "boot":
            entry["load_stage"] = "runtime"
            entry["load_policy"] = "automatic"
    path.write_text(json.dumps(policy, indent=2) + "\n", encoding="utf-8")


def build_esp(repo: Path, output: Path, drivers: list[Path]) -> subprocess.CompletedProcess:
    command = [
        "python3", "tools/build_uefi_esp.py", "--efi", "bin/BOOTX64.EFI",
        "--kernel", "bin/kernel64.bin", "--root", "bin/os64.bin",
    ]
    for driver in drivers:
        command += ["--boot-driver", str(driver)]
    command += ["--output", str(output)]
    return subprocess.run(command, cwd=repo, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)


def main() -> int:
    source = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="os64-boot-driver-") as temporary:
        repo = Path(temporary) / "repo"
        repo.mkdir()
        copy_worktree(source, repo)

        set_boot_policy(repo, {"hello_c"})
        subprocess.run(["make", "-j2", "uefi"], cwd=repo, check=True,
                       stdout=subprocess.DEVNULL)
        normal = boot(repo, repo / "bin/uefi_esp.img", "success")
        require("Boot module count: 0x00000001" in normal, "signed boot module was not handed off")
        require("[drv] hello_c.drv driver_entry()" in normal, "boot module entry did not run")
        require("hello_c kind=module state=ready" in normal, "boot module did not become ready")
        require(normal.index("[drv] hello_c.drv driver_entry()") < normal.index("Root source:"),
                "boot module activated after kernel-stage storage initialization")

        unsigned = repo / "build/driver_pkg_hello_c.unsigned.drv"
        unsigned_esp = repo / "bin/uefi_unsigned.img"
        require(build_esp(repo, unsigned_esp, [unsigned]).returncode == 0,
                "could not construct unsigned rejection image")
        unsigned_log = boot(repo, unsigned_esp, "unsigned")
        require("Boot module count: 0x00000000" in unsigned_log, "UEFI accepted unsigned boot module")
        require("[drv] hello_c.drv driver_entry()" not in unsigned_log,
                "unsigned boot module entry executed")

        tampered = repo / "bin/hello_c_tampered.drv"
        tampered_bytes = bytearray((repo / "bin/hello_c.drv").read_bytes())
        tampered_bytes[200] ^= 0x5A
        tampered.write_bytes(tampered_bytes)
        tampered_esp = repo / "bin/uefi_tampered.img"
        require(build_esp(repo, tampered_esp, [tampered]).returncode == 0,
                "could not construct tampered rejection image")
        tampered_log = boot(repo, tampered_esp, "tampered")
        require("Boot module count: 0x00000000" in tampered_log, "UEFI accepted tampered boot module")

        oversized = repo / "bin/oversized.drv"
        oversized.write_bytes(bytes(1024 * 1024 + 1))
        require(build_esp(repo, repo / "bin/uefi_oversized.img", [oversized]).returncode != 0,
                "ESP builder accepted oversized boot module")
        require(build_esp(repo, repo / "bin/uefi_too_many.img",
                          [repo / "bin/hello_c.drv"] * 9).returncode != 0,
                "ESP builder accepted more than eight boot modules")

        set_boot_policy(repo, {"provider_c", "consumer_c"})
        subprocess.run(["make", "-j2", "uefi"], cwd=repo, check=True,
                       stdout=subprocess.DEVNULL)
        ordered = boot(repo, repo / "bin/uefi_esp.img", "dependency_success")
        require("Boot module count: 0x00000002" in ordered, "dependency boot modules missing")
        require(ordered.index("provider_c.drv driver_entry()") < ordered.index("consumer_c.drv driver_entry()"),
                "boot dependency order was not preserved")

        missing_dep_esp = repo / "bin/uefi_missing_dependency.img"
        require(build_esp(repo, missing_dep_esp, [repo / "bin/consumer_c.drv"]).returncode == 0,
                "could not construct dependency rejection image")
        missing_dep = boot(repo, missing_dep_esp, "dependency_reject")
        require("Boot module count: 0x00000001" in missing_dep, "dependency test module was not handed off")
        require("consumer_c kind=module state=ready" not in missing_dep,
                "dependency-invalid boot module became ready")
        require("OS64>" in missing_dep, "dependency rejection corrupted boot state")

    print("boot driver handoff smoke OK (signed, unsigned, tampered, bounded, dependency)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
