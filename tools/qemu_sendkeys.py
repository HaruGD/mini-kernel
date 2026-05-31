#!/usr/bin/env python3
import argparse
import os
import socket
import subprocess
import sys
import time


def wait_for_path(path: str, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {path}")


def wait_for_serial_contains(path: str, needle: bytes, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(path, "rb") as serial_file:
                if needle in serial_file.read():
                    return
        except FileNotFoundError:
            pass
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for serial output {needle!r}")


def send_monitor_commands(sock_path: str, commands: list[tuple[str, float]]) -> None:
    deadline = time.time() + 5.0
    last_error = None
    while time.time() < deadline:
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.connect(sock_path)
                sock.settimeout(0.2)
                try:
                    sock.recv(4096)
                except OSError:
                    pass
                for command, delay in commands:
                    sock.sendall((command + "\n").encode("ascii"))
                    try:
                        sock.recv(4096)
                    except OSError:
                        pass
                    time.sleep(delay)
                return
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    if last_error is not None:
        raise last_error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", default="./bin/os64.bin")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--shell-command", default="ushell")
    parser.add_argument("--shell-prompt", default="csh>")
    parser.add_argument("--prompt-timeout", type=float, default=10.0)
    parser.add_argument("--delay", type=float, default=0.6)
    parser.add_argument("--command", dest="commands", action="append", default=[])
    args = parser.parse_args()

    mon_path = "/tmp/os64-qemu-mon.sock"
    serial_path = "/tmp/os64-qemu-serial.log"
    qemu_out_path = "/tmp/os64-qemu-out.log"
    for path in (mon_path, serial_path, qemu_out_path):
        try:
            os.remove(path)
        except FileNotFoundError:
            pass

    with open(qemu_out_path, "wb") as qemu_out:
        proc = subprocess.Popen(
            [
                "qemu-system-x86_64",
                "-display",
                "none",
                "-drive",
                f"format=raw,file={args.image},if=ide,index=0",
                "-serial",
                f"file:{serial_path}",
                "-monitor",
                f"unix:{mon_path},server,nowait",
            ],
            stdout=qemu_out,
            stderr=subprocess.STDOUT,
        )

    try:
        wait_for_path(mon_path, 5.0)
        def send_text(text: str, delay: float = 0.05) -> None:
            commands: list[tuple[str, float]] = []
            key_map = {" ": "spc", ".": "dot", "_": "shift-minus", "/": "slash"}
            for char in text:
                key = key_map.get(char, char.lower())
                commands.append((f"sendkey {key}", delay))
            send_monitor_commands(mon_path, commands)

        wait_for_serial_contains(serial_path, b"OS64>", args.prompt_timeout)
        send_text(args.shell_command)
        send_monitor_commands(mon_path, [("sendkey ret", 0.10)])
        wait_for_serial_contains(serial_path, args.shell_prompt.encode("ascii"), args.prompt_timeout)

        commands = args.commands or [
            "version",
            "uptime",
            "touch note.txt",
            "save note.txt hi",
            "cat note.txt",
            "rm note.txt",
            "exit",
        ]

        for command in commands:
            send_text(command)
            send_monitor_commands(mon_path, [("sendkey ret", 0.10)])
            time.sleep(args.delay)

        send_monitor_commands(mon_path, [("quit", 0.10)])
        proc.wait(timeout=args.timeout)
    except Exception:
        proc.kill()
        proc.wait(timeout=2.0)
        raise

    with open(serial_path, "rb") as serial_file:
        sys.stdout.buffer.write(serial_file.read())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
