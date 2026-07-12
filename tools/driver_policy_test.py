#!/usr/bin/env python3
import json
import shutil
import tempfile
from pathlib import Path

from driver_policy import ALLOWED_COMBINATIONS, validate_policy


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def make_fixture(root: Path):
    entries = []
    settings_paths = {}
    combinations = sorted(ALLOWED_COMBINATIONS)
    for index, (artifact, stage, policy) in enumerate(combinations):
        name = f"driver_{index}"
        relative = f"demo/{name}"
        directory = root / "drivers" / "demo" / name
        directory.mkdir(parents=True)
        (directory / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
        settings = {
            "schema_version": 1,
            "name": name,
            "display_name": f"Driver {index}",
            "version": "1.0.0",
            "description": "Policy validator fixture.",
            "domain": "demo",
            "permissions": [],
            "dependencies": [],
            "exports": [],
        }
        if artifact == "drv":
            settings["entry"] = "driver_entry"
        else:
            settings["linked"] = {
                "priority": index,
                "state": "ready",
                "instance": "0",
                "includes": [],
                "externs": [],
            }
        settings_path = directory / "settings.json"
        write_json(settings_path, settings)
        settings_paths[name] = settings_path
        entries.append({
            "name": name,
            "path": relative,
            "enabled": True,
            "artifact": artifact,
            "load_stage": stage,
            "load_policy": policy,
        })
    policy = {"schema_version": 1, "drivers": entries}
    write_json(root / "config/drivers.json", policy)
    return policy, settings_paths


def fixture(parent: Path, name: str):
    root = parent / name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()
    policy, settings = make_fixture(root)
    return root, policy, settings


def expect_invalid(root: Path, needle: str) -> None:
    errors = validate_policy(root)
    if not any(needle in error for error in errors):
        raise AssertionError(f"expected {needle!r}, got {errors!r}")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    for schema in (
        repo / "config/schemas/driver-settings.schema.json",
        repo / "config/schemas/drivers-policy.schema.json",
    ):
        read_json(schema)

    with tempfile.TemporaryDirectory(prefix="os64_driver_policy_") as directory:
        parent = Path(directory)

        root, policy, settings = fixture(parent, "valid")
        assert validate_policy(root) == []
        policy["drivers"][0]["enabled"] = False
        write_json(root / "config/drivers.json", policy)
        assert validate_policy(root) == []

        root, policy, _ = fixture(parent, "schema_boolean")
        policy["schema_version"] = True
        write_json(root / "config/drivers.json", policy)
        expect_invalid(root, "schema_version must be 1")

        root, policy, _ = fixture(parent, "forbidden")
        policy["drivers"][0].update({
            "artifact": "linked", "load_stage": "runtime", "load_policy": "manual"
        })
        write_json(root / "config/drivers.json", policy)
        expect_invalid(root, "forbidden artifact/stage/policy combination")

        root, policy, _ = fixture(parent, "unknown_artifact")
        policy["drivers"][0]["artifact"] = "module"
        write_json(root / "config/drivers.json", policy)
        expect_invalid(root, "unknown artifact")

        root, policy, _ = fixture(parent, "missing_directory")
        policy["drivers"][0]["path"] = "demo/missing"
        write_json(root / "config/drivers.json", policy)
        expect_invalid(root, "missing driver directory")

        root, policy, settings = fixture(parent, "name_mismatch")
        local = read_json(settings[policy["drivers"][0]["name"]])
        local["name"] = "different_name"
        write_json(settings[policy["drivers"][0]["name"]], local)
        expect_invalid(root, "does not match settings name")

        root, policy, _ = fixture(parent, "missing_makefile")
        target = root / "drivers" / policy["drivers"][0]["path"] / "Makefile"
        target.unlink()
        expect_invalid(root, "missing Makefile")

        root, policy, settings = fixture(parent, "later_dependency")
        boot = next(entry for entry in policy["drivers"] if entry["load_stage"] == "boot" and entry["load_policy"] == "automatic")
        runtime = next(entry for entry in policy["drivers"] if entry["load_stage"] == "runtime")
        local = read_json(settings[boot["name"]])
        local["dependencies"] = [runtime["name"]]
        write_json(settings[boot["name"]], local)
        expect_invalid(root, "is in a later stage")

        root, policy, settings = fixture(parent, "automatic_manual")
        automatic = next(entry for entry in policy["drivers"] if entry["artifact"] == "drv" and entry["load_stage"] == "runtime" and entry["load_policy"] == "automatic")
        manual = next(entry for entry in policy["drivers"] if entry["load_policy"] == "manual")
        local = read_json(settings[automatic["name"]])
        local["dependencies"] = [manual["name"]]
        write_json(settings[automatic["name"]], local)
        expect_invalid(root, "automatic driver depends on manual driver")

        root, policy, settings = fixture(parent, "cycle")
        first, second = policy["drivers"][0], policy["drivers"][1]
        first_settings = read_json(settings[first["name"]])
        second_settings = read_json(settings[second["name"]])
        first_settings["dependencies"] = [second["name"]]
        second_settings["dependencies"] = [first["name"]]
        write_json(settings[first["name"]], first_settings)
        write_json(settings[second["name"]], second_settings)
        expect_invalid(root, "driver dependency cycle")

        root, _, _ = fixture(parent, "unlisted")
        extra = root / "drivers/demo/extra"
        extra.mkdir(parents=True)
        (extra / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
        write_json(extra / "settings.json", {
            "schema_version": 1, "name": "extra", "display_name": "Extra",
            "version": "1.0.0", "description": "Unlisted fixture.", "domain": "demo",
            "entry": "driver_entry", "permissions": [], "dependencies": [], "exports": [],
        })
        expect_invalid(root, "unlisted driver settings")

        root, _, _ = fixture(parent, "duplicate_json")
        (root / "config/drivers.json").write_text(
            '{"schema_version":1,"schema_version":1,"drivers":[]}\n', encoding="utf-8"
        )
        expect_invalid(root, "duplicate JSON key")

    print("driver policy test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
