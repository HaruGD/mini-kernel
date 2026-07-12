#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path

from driver_policy import validate_policy


def find_project(root: Path, driver_dir: Path):
    policy = json.loads((root / "config/drivers.json").read_text(encoding="utf-8"))
    relative = driver_dir.relative_to(root / "drivers").as_posix()
    matches = [entry for entry in policy["drivers"] if entry["path"] == relative]
    if len(matches) != 1:
        raise SystemExit(f"{relative}: expected exactly one central policy entry")
    settings = json.loads((driver_dir / "settings.json").read_text(encoding="utf-8"))
    return matches[0], settings


def package(root: Path, driver_dir: Path, entry: dict, settings: dict) -> None:
    if entry["artifact"] != "drv" or not entry["enabled"]:
        raise SystemExit(f"{entry['name']}: package target is disabled by central policy")
    sources = sorted(list(driver_dir.glob("*.c")) + list(driver_dir.glob("*.cpp")) +
                     list((driver_dir / "src").glob("*.c")) + list((driver_dir / "src").glob("*.cpp")))
    if len(sources) != 1:
        raise SystemExit(f"{driver_dir}: packaged drivers currently require exactly one C/C++ source")
    build = root / "build"
    generated = build / "generated/manifests"
    output = root / "bin" / f"{entry['name']}.drv"
    obj = build / f"driver_pkg_{entry['name']}.o"
    unsigned = build / f"driver_pkg_{entry['name']}.unsigned.drv"
    generated.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest = {key: settings[key] for key in (
        "name", "version", "entry", "permissions", "boot_modes", "dependencies", "exports", "imports"
    ) if key in settings}
    if entry["load_policy"] == "manual":
        manifest["flags"] = ["NO_AUTOLOAD"]
    manifest_path = generated / f"{entry['name']}.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    compiler = "g++" if sources[0].suffix == ".cpp" else "gcc"
    std = "-std=gnu++17" if compiler == "g++" else "-std=gnu11"
    flags = ["-g", "-ffreestanding", "-nostdlib", "-nostartfiles", "-nodefaultlibs", "-Wall", "-O0", std,
             "-m64", "-mcmodel=large", "-mno-red-zone", "-fno-pic", "-fno-pie", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fomit-frame-pointer",
             "-I" + str(root / "drivers/include")]
    if compiler == "g++":
        flags += ["-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit"]
    subprocess.run([compiler, *flags, "-c", str(sources[0]), "-o", str(obj)], check=True)
    subprocess.run(["python3", str(root / "tools/driver_builder/build_drv.py"), "--object", str(obj),
                    "--output", str(unsigned), "--manifest", str(manifest_path)], check=True)
    subprocess.run(["python3", str(root / "tools/driver_builder/sign_drv.py"), "--input", str(unsigned),
                    "--output", str(output), "--algorithm", "local-test"], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver-dir", type=Path, required=True)
    parser.add_argument("--action", choices=("info", "artifact", "linked", "package"), required=True)
    args = parser.parse_args()
    driver_dir = args.driver_dir.resolve()
    root = Path(__file__).resolve().parents[1]
    errors = validate_policy(root)
    if errors:
        raise SystemExit("driver policy validation failed:\n- " + "\n- ".join(errors))
    entry, settings = find_project(root, driver_dir)
    if args.action == "info":
        label = "packaged" if entry["artifact"] == "drv" else "linked"
        print(f"{label} driver: {entry['name']}")
        print(f"settings: {driver_dir / 'settings.json'}")
        print(f"stage: {entry['load_stage']} ({entry['load_policy']})")
        return 0
    action = entry["artifact"] if args.action == "artifact" else args.action
    requested_artifact = "drv" if action == "package" else action
    if requested_artifact != entry["artifact"]:
        raise SystemExit(f"{entry['name']}: {action} is not selected by central policy")
    if requested_artifact == "linked":
        objects = settings["linked"]["objects"]
        subprocess.run(["make", "-C", str(root), *objects], check=True)
    else:
        package(root, driver_dir, entry, settings)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
