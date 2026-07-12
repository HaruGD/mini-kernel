#!/usr/bin/env python3
import json
import re
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def check_links(root: Path, document: Path, failures: list[str]) -> None:
    text = document.read_text(encoding="utf-8")
    for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", text):
        if target.startswith(("http://", "https://", "#")):
            continue
        path_text = target.split("#", 1)[0]
        if not path_text:
            continue
        resolved = (document.parent / path_text).resolve()
        require(resolved.is_relative_to(root) and resolved.exists(),
                f"{document.relative_to(root)}: broken link {target}", failures)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    readme = (root / "README.md").read_text(encoding="utf-8")
    abi = (root / "docs/driver_abi.md").read_text(encoding="utf-8")
    phase = (root / "docs/phase3_6_driver_packaging.md").read_text(encoding="utf-8")
    matrix_path = root / "docs/phase3_6_regression_matrix.md"
    matrix = matrix_path.read_text(encoding="utf-8")
    makefile = (root / "Makefile").read_text(encoding="utf-8")

    for row in range(1, 13):
        require(f"| R{row:02d} |" in matrix, f"matrix row R{row:02d} missing", failures)
    for text in ("settings.json", "Makefile", "config/drivers.json", "test-driver-regression"):
        require(text in readme, f"README responsibility/reference missing: {text}", failures)
    for stale in ("external driver autoload", "drivers/builtin", "drivers/external",
                  "produced from `drivers/demo/*/driver.c`, `driver.json`"):
        require(stale not in readme, f"README retains stale model: {stale}", failures)
    for text in ("boot-stage `.drv`", "kernel-stage packages", "runtime-automatic",
                 "runtime-manual", "config/drivers.json"):
        require(text in abi, f"Driver ABI staged policy text missing: {text}", failures)

    require("- [x] **D11:" in phase and "- [x] **D12:" in phase,
            "D11/D12 are not marked complete", failures)
    entry = phase.split("## Phase 4 Entry Criteria", 1)[-1]
    require("- [ ]" not in entry, "Phase 4 entry criteria remain unchecked", failures)
    require("test-driver-regression: test-driver-build test-uefi-smoke test-user-sdk" in makefile,
            "complete driver regression target is not wired", failures)
    require("test-closure: test-abi-freeze test-driver-regression" in makefile,
            "driver regression target is not part of closure", failures)

    policy = json.loads((root / "config/drivers.json").read_text(encoding="utf-8"))
    enabled = [entry for entry in policy["drivers"] if entry["enabled"]]
    automatic = {entry["name"] for entry in enabled if entry["artifact"] == "drv" and entry["load_policy"] == "automatic"}
    manual = {entry["name"] for entry in enabled if entry["artifact"] == "drv" and entry["load_policy"] == "manual"}
    require(automatic == {"provider_c", "consumer_c", "hello_c", "hello_cpp", "irq_timer_c", "pci_probe_c"},
            "runtime automatic package baseline drifted", failures)
    require(manual == {"gop_demo_c"}, "runtime manual package baseline drifted", failures)
    require(not list((root / "drivers").rglob("driver.json")), "legacy driver.json returned", failures)

    with tempfile.TemporaryDirectory(prefix="os64-driver-regression-") as temporary:
        out = Path(temporary)
        subprocess.run([
            "python3", str(root / "tools/gen_driver_build.py"),
            "--make-output", str(out / "policy.mk"),
            "--registry-output", str(out / "registry.cpp"),
            "--activation-output", str(out / "activation.cpp"),
        ], cwd=root, check=True, stdout=subprocess.DEVNULL)
        generated_make = (out / "policy.mk").read_text(encoding="utf-8")
        registry = (out / "registry.cpp").read_text(encoding="utf-8")
        activation = (out / "activation.cpp").read_text(encoding="utf-8")
        require("MANUAL_DRIVER_PACKAGES := ./bin/gop_demo_c.drv" in generated_make,
                "generated manual shipped set drifted", failures)
        require('"/gop_demo_c.drv"' not in activation,
                "manual package entered automatic activation", failures)
        for name in automatic:
            require(f'"/{name}.drv"' in activation, f"automatic package missing: {name}", failures)
        for stage in ("boot", "kernel", "runtime"):
            require(f"driver_manager_activate_linked_{stage}" in registry,
                    f"linked {stage} function missing", failures)

    uefi = (root / "boot/uefi/uefi_boot.c").read_text(encoding="utf-8")
    boot_info = (root / "include/kernel/boot_info.h").read_text(encoding="utf-8")
    smoke = (root / "tools/uefi_smoke.py").read_text(encoding="utf-8")
    require("verify_boot_driver" in uefi and "BOOT_DRIVER_MAX_SIZE" in uefi,
            "UEFI boot module verification missing", failures)
    require("BOOT_MODULE_MAX 8" in boot_info and "BOOT_INFO_VERSION 3" in boot_info,
            "BootInfo v3 bounded module ABI missing", failures)
    for command in ("drvload", "drvunload", "drvreload", "drivers"):
        require(command in smoke, f"UEFI smoke command missing: {command}", failures)
    for token in ("Root source: ramdisk", "fat32 kind=fs state=ready", "gop_demo_c.drv GOP draw OK"):
        require(token in smoke, f"UEFI smoke assertion missing: {token}", failures)

    for document in (root / "README.md", root / "docs/driver_abi.md",
                     root / "docs/driver_policy.md", matrix_path):
        check_links(root, document, failures)

    if failures:
        print("driver regression matrix test failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("driver regression matrix test OK (R01-R12)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
