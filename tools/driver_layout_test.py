#!/usr/bin/env python3
import json
import subprocess
from pathlib import Path

from driver_policy import validate_policy


EXPECTED_DOMAINS = {"block", "bus", "display", "fs", "input", "timer", "demo"}


def check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures = validate_policy(root)
    policy = json.loads((root / "config/drivers.json").read_text(encoding="utf-8"))

    check(not (root / "drivers/builtin").exists(), "legacy drivers/builtin still exists", failures)
    check(not (root / "drivers/external").exists(), "legacy drivers/external still exists", failures)
    check(not (root / "fs/fat32").exists(), "legacy fs/fat32 still exists", failures)
    check(not (root / "include/fs/fat32.h").exists(), "legacy FAT32 header still exists", failures)
    check((root / "drivers/include/os64_driver.h").is_file(), "shared driver SDK header missing", failures)

    enabled_names = set()
    for entry in policy["drivers"]:
        if entry["enabled"]:
            enabled_names.add(entry["name"])
        path = Path(entry["path"])
        check(path.parts[0] in EXPECTED_DOMAINS, f"{entry['name']}: non-domain path", failures)
        directory = root / "drivers" / path
        for filename in ("settings.json", "Makefile"):
            check((directory / filename).is_file(), f"{entry['name']}: missing {filename}", failures)
        check(not (directory / "driver.json").exists(), f"{entry['name']}: legacy driver.json remains", failures)
        result = subprocess.run(
            ["make", "-s", "-C", str(directory), "info"],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        check(result.returncode == 0, f"{entry['name']}: local Makefile info failed: {result.stdout}", failures)
        expected_kind = "linked driver:" if entry["artifact"] == "linked" else "packaged driver:"
        check(expected_kind in result.stdout, f"{entry['name']}: wrong local Makefile mode", failures)

    settings_names = {
        json.loads(path.read_text(encoding="utf-8"))["name"]
        for path in (root / "drivers").rglob("settings.json")
    }
    check(enabled_names == settings_names, "enabled policy/settings driver set differs", failures)

    fat32 = root / "drivers/fs/fat32"
    for relative in (
        "src/fat32.cpp", "src/fat32_vfs.cpp", "include/fat32.h",
        "settings.json", "Makefile",
    ):
        check((fat32 / relative).is_file(), f"FAT32 package missing {relative}", failures)

    if failures:
        print("driver layout test failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("driver layout test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
