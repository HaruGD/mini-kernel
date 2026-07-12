#!/usr/bin/env python3
import json
import subprocess
import tempfile
from pathlib import Path


COMBINATIONS = (
    ("linked_boot", "linked", "boot", "automatic"),
    ("linked_kernel", "linked", "kernel", "automatic"),
    ("linked_runtime", "linked", "runtime", "automatic"),
    ("drv_boot", "drv", "boot", "automatic"),
    ("drv_kernel", "drv", "kernel", "automatic"),
    ("drv_runtime", "drv", "runtime", "automatic"),
    ("drv_manual", "drv", "runtime", "manual"),
)


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"driver build integration test failed: {message}")


def make_fixture(root: Path) -> None:
    entries = []
    for index, (name, artifact, stage, policy) in enumerate(COMBINATIONS):
        relative = f"demo/{name}"
        directory = root / "drivers" / relative
        directory.mkdir(parents=True)
        (directory / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
        settings = {
            "schema_version": 1, "name": name, "display_name": name,
            "version": "1.0.0", "description": name, "domain": "demo",
            "entry": "driver_entry", "permissions": [], "dependencies": [], "exports": [],
            "linked": {
                "priority": index, "state": "ready", "instance": "0",
                "includes": [], "externs": [], "objects": [f"./build/{name}.o"],
            },
        }
        write_json(directory / "settings.json", settings)
        entries.append({"name": name, "path": relative, "enabled": True, "artifact": artifact,
                        "load_stage": stage, "load_policy": policy})
    disabled = root / "drivers/demo/disabled"
    disabled.mkdir(parents=True)
    (disabled / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
    write_json(disabled / "settings.json", {
        "schema_version": 1, "name": "disabled", "display_name": "disabled", "version": "1.0.0",
        "description": "disabled", "domain": "demo", "entry": "driver_entry", "permissions": [],
        "dependencies": [], "exports": [],
    })
    entries.append({"name": "disabled", "path": "demo/disabled", "enabled": False,
                    "artifact": "drv", "load_stage": "runtime", "load_policy": "manual"})
    write_json(root / "config/drivers.json", {"schema_version": 1, "drivers": entries})


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="os64-driver-build-") as temporary:
        fixture = Path(temporary)
        make_fixture(fixture)
        make_out = fixture / "build/policy.mk"
        registry_out = fixture / "build/registry.cpp"
        activation_out = fixture / "build/activation.cpp"
        subprocess.run([
            "python3", str(repo / "tools/gen_driver_build.py"), "--root", str(fixture),
            "--make-output", str(make_out), "--registry-output", str(registry_out),
            "--activation-output", str(activation_out),
        ], check=True)
        make_text = make_out.read_text(encoding="utf-8")
        registry = registry_out.read_text(encoding="utf-8")
        activation = activation_out.read_text(encoding="utf-8")

        require("./build/linked_boot.o" in make_text and "./build/linked_runtime.o" in make_text,
                "linked object list is not policy-generated")
        require("BOOT_DRIVER_PACKAGES := ./bin/drv_boot.drv" in make_text, "boot package set is wrong")
        require("KERNEL_DRIVER_PACKAGES := ./bin/drv_kernel.drv" in make_text, "kernel package set is wrong")
        require("RUNTIME_DRIVER_PACKAGES := ./bin/drv_runtime.drv ./bin/drv_manual.drv" in make_text,
                "runtime shipped set is wrong")
        require("MANUAL_DRIVER_PACKAGES := ./bin/drv_manual.drv" in make_text, "manual set is wrong")
        require("disabled.drv" not in make_text, "disabled driver entered the build")

        for stage in ("boot", "kernel", "runtime"):
            require(f"driver_manager_activate_linked_{stage}" in registry, f"missing linked {stage} plan")
            body = registry.split(f"driver_manager_activate_linked_{stage}()", 1)[1].split("}", 1)[0]
            require(f'"linked_{stage}"' in body, f"linked {stage} driver entered wrong plan")
        require('"drv_boot"' in activation, "boot package allow-list missing")
        require('"/drv_kernel.drv"' in activation, "kernel automatic package missing")
        require('"/drv_runtime.drv"' in activation, "runtime automatic package missing")
        require('"/drv_manual.drv"' not in activation, "manual package entered automatic plan")

    boot_info = (repo / "include/kernel/boot_info.h").read_text(encoding="utf-8")
    uefi = (repo / "boot/uefi/uefi_boot.c").read_text(encoding="utf-8")
    require("BOOT_MODULE_MAX" in boot_info and "BootModule boot_modules" in boot_info,
            "bounded BootInfo module list missing")
    require("verify_boot_driver" in uefi and "DRV_SIGNATURE_ALG_LOCAL_TEST" in uefi,
            "UEFI boot package verification missing")
    require("BOOT_DRIVER_MAX_SIZE" in uefi, "UEFI boot package size bound missing")
    print("driver build integration test OK (7 policy combinations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
