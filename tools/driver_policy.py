#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path, PurePosixPath


NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PERMISSION_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
DOMAINS = {"block", "bus", "display", "fs", "input", "timer", "demo"}
ARTIFACTS = {"linked", "drv"}
STAGES = {"boot", "kernel", "runtime"}
POLICIES = {"automatic", "manual"}
STAGE_RANK = {"boot": 0, "kernel": 1, "runtime": 2}
ALLOWED_COMBINATIONS = {
    ("linked", "boot", "automatic"),
    ("linked", "kernel", "automatic"),
    ("linked", "runtime", "automatic"),
    ("drv", "boot", "automatic"),
    ("drv", "kernel", "automatic"),
    ("drv", "runtime", "automatic"),
    ("drv", "runtime", "manual"),
}
POLICY_FIELDS = {"name", "path", "enabled", "artifact", "load_stage", "load_policy"}
SETTINGS_FIELDS = {
    "schema_version", "name", "display_name", "version", "description", "domain",
    "entry", "permissions", "boot_modes", "dependencies", "exports", "imports", "linked",
}
SETTINGS_REQUIRED = {
    "schema_version", "name", "display_name", "version", "description", "domain",
    "permissions", "dependencies", "exports",
}
LINKED_FIELDS = {"priority", "state", "instance", "includes", "externs", "objects"}


class DuplicateKeyError(ValueError):
    pass


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_json(path: Path, errors: list[str]):
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys)
    except (OSError, json.JSONDecodeError, DuplicateKeyError) as error:
        errors.append(f"{path}: invalid JSON: {error}")
        return None


def check_exact_fields(value: dict, required: set[str], allowed: set[str], label: str, errors: list[str]) -> None:
    missing = sorted(required - set(value))
    unknown = sorted(set(value) - allowed)
    for field in missing:
        errors.append(f"{label}: missing field {field!r}")
    for field in unknown:
        errors.append(f"{label}: unknown field {field!r}")


def check_string_list(value, label: str, errors: list[str], pattern=None) -> None:
    if not isinstance(value, list):
        errors.append(f"{label}: expected an array")
        return
    seen = set()
    for index, item in enumerate(value):
        if not isinstance(item, str) or item == "":
            errors.append(f"{label}[{index}]: expected a non-empty string")
            continue
        if pattern is not None and pattern.fullmatch(item) is None:
            errors.append(f"{label}[{index}]: invalid value {item!r}")
        if item in seen:
            errors.append(f"{label}: duplicate value {item!r}")
        seen.add(item)


def validate_settings(settings, label: str, errors: list[str]) -> None:
    if not isinstance(settings, dict):
        errors.append(f"{label}: root must be an object")
        return
    check_exact_fields(settings, SETTINGS_REQUIRED, SETTINGS_FIELDS, label, errors)
    if type(settings.get("schema_version")) is not int or settings.get("schema_version") != 1:
        errors.append(f"{label}: schema_version must be 1")
    name = settings.get("name")
    if not isinstance(name, str) or NAME_RE.fullmatch(name) is None:
        errors.append(f"{label}: invalid driver name")
    for field in ("display_name", "description"):
        if not isinstance(settings.get(field), str) or settings.get(field) == "":
            errors.append(f"{label}: {field} must be a non-empty string")
    version = settings.get("version")
    if not isinstance(version, str) or VERSION_RE.fullmatch(version) is None:
        errors.append(f"{label}: version must use MAJOR.MINOR.PATCH")
    if settings.get("domain") not in DOMAINS:
        errors.append(f"{label}: unknown domain {settings.get('domain')!r}")
    check_string_list(settings.get("permissions"), f"{label}.permissions", errors, PERMISSION_RE)
    check_string_list(settings.get("dependencies"), f"{label}.dependencies", errors, NAME_RE)
    check_string_list(settings.get("exports"), f"{label}.exports", errors, SYMBOL_RE)
    if "boot_modes" in settings:
        check_string_list(settings["boot_modes"], f"{label}.boot_modes", errors)
        if isinstance(settings["boot_modes"], list):
            for mode in settings["boot_modes"]:
                if mode not in {"NORMAL", "DIAGNOSTIC", "RECOVERY"}:
                    errors.append(f"{label}.boot_modes: unknown mode {mode!r}")
    if "entry" in settings and (not isinstance(settings["entry"], str) or SYMBOL_RE.fullmatch(settings["entry"]) is None):
        errors.append(f"{label}: invalid entry symbol")
    imports = settings.get("imports")
    if imports is not None:
        if not isinstance(imports, dict):
            errors.append(f"{label}.imports: expected an object")
        else:
            for symbol, permissions in imports.items():
                if not isinstance(symbol, str) or re.fullmatch(r"^[A-Za-z_][A-Za-z0-9_.]*$", symbol) is None:
                    errors.append(f"{label}.imports: invalid symbol {symbol!r}")
                check_string_list(permissions, f"{label}.imports[{symbol!r}]", errors, PERMISSION_RE)
    linked = settings.get("linked")
    if linked is not None:
        if not isinstance(linked, dict):
            errors.append(f"{label}.linked: expected an object")
        else:
            check_exact_fields(linked, LINKED_FIELDS, LINKED_FIELDS, f"{label}.linked", errors)
            if not isinstance(linked.get("priority"), int) or isinstance(linked.get("priority"), bool) or linked.get("priority", -1) < 0:
                errors.append(f"{label}.linked: priority must be a non-negative integer")
            for field in ("state", "instance"):
                if not isinstance(linked.get(field), str) or linked.get(field) == "":
                    errors.append(f"{label}.linked: {field} must be a non-empty string")
            check_string_list(linked.get("includes"), f"{label}.linked.includes", errors)
            check_string_list(linked.get("externs"), f"{label}.linked.externs", errors)
            check_string_list(linked.get("objects"), f"{label}.linked.objects", errors)
            if isinstance(linked.get("objects"), list) and not linked["objects"]:
                errors.append(f"{label}.linked: objects must not be empty")


def validate_policy(root: Path, config_path: Path | None = None) -> list[str]:
    root = root.resolve()
    drivers_root = (root / "drivers").resolve()
    config_path = (config_path or root / "config/drivers.json").resolve()
    errors: list[str] = []
    policy = load_json(config_path, errors)
    if policy is None:
        return errors
    if not isinstance(policy, dict):
        return [f"{config_path}: root must be an object"]
    check_exact_fields(policy, {"schema_version", "drivers"}, {"schema_version", "drivers"}, str(config_path), errors)
    if type(policy.get("schema_version")) is not int or policy.get("schema_version") != 1:
        errors.append(f"{config_path}: schema_version must be 1")
    entries = policy.get("drivers")
    if not isinstance(entries, list) or len(entries) == 0:
        errors.append(f"{config_path}: drivers must be a non-empty array")
        return errors

    policies = {}
    settings_by_name = {}
    paths = set()
    referenced_paths = set()
    for index, entry in enumerate(entries):
        label = f"{config_path}: drivers[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label}: expected an object")
            continue
        check_exact_fields(entry, POLICY_FIELDS, POLICY_FIELDS, label, errors)
        name = entry.get("name")
        path_text = entry.get("path")
        artifact = entry.get("artifact")
        stage = entry.get("load_stage")
        load_policy = entry.get("load_policy")
        if not isinstance(name, str) or NAME_RE.fullmatch(name) is None:
            errors.append(f"{label}: invalid driver name")
            continue
        if name in policies:
            errors.append(f"{label}: duplicate driver name {name!r}")
        else:
            policies[name] = entry
        if not isinstance(entry.get("enabled"), bool):
            errors.append(f"{label}: enabled must be boolean")
        if artifact not in ARTIFACTS:
            errors.append(f"{label}: unknown artifact {artifact!r}")
        if stage not in STAGES:
            errors.append(f"{label}: unknown load_stage {stage!r}")
        if load_policy not in POLICIES:
            errors.append(f"{label}: unknown load_policy {load_policy!r}")
        if (artifact, stage, load_policy) not in ALLOWED_COMBINATIONS:
            errors.append(f"{label}: forbidden artifact/stage/policy combination")
        if not isinstance(path_text, str) or re.fullmatch(r"^[a-z0-9_]+(?:/[a-z0-9_]+)*$", path_text) is None:
            errors.append(f"{label}: invalid relative driver path")
            continue
        pure_path = PurePosixPath(path_text)
        driver_dir = (drivers_root / Path(*pure_path.parts)).resolve()
        if not driver_dir.is_relative_to(drivers_root):
            errors.append(f"{label}: driver path escapes drivers root")
            continue
        if path_text in paths:
            errors.append(f"{label}: duplicate driver path {path_text!r}")
        paths.add(path_text)
        referenced_paths.add(path_text)
        if not driver_dir.is_dir():
            errors.append(f"{label}: missing driver directory {path_text!r}")
            continue
        settings_path = driver_dir / "settings.json"
        makefile_path = driver_dir / "Makefile"
        if not settings_path.is_file():
            errors.append(f"{label}: missing settings.json")
            continue
        if not makefile_path.is_file():
            errors.append(f"{label}: missing Makefile")
        settings = load_json(settings_path, errors)
        if settings is None:
            continue
        validate_settings(settings, str(settings_path), errors)
        local_name = settings.get("name") if isinstance(settings, dict) else None
        if local_name != name:
            errors.append(f"{label}: policy name {name!r} does not match settings name {local_name!r}")
        if artifact == "linked" and not isinstance(settings.get("linked"), dict):
            errors.append(f"{label}: linked artifact requires settings linked block")
        if artifact == "drv" and not isinstance(settings.get("entry"), str):
            errors.append(f"{label}: drv artifact requires settings entry symbol")
        settings_by_name[name] = settings

    discovered_paths = {
        settings.parent.relative_to(drivers_root).as_posix()
        for settings in drivers_root.rglob("settings.json")
    } if drivers_root.is_dir() else set()
    for path in sorted(discovered_paths - referenced_paths):
        errors.append(f"unlisted driver settings: {path!r}")
    for path in sorted(referenced_paths - discovered_paths):
        errors.append(f"policy path has no settings: {path!r}")

    for name, settings in settings_by_name.items():
        entry = policies.get(name)
        if entry is None or not isinstance(settings, dict):
            continue
        dependencies = settings.get("dependencies", [])
        if not isinstance(dependencies, list):
            continue
        for dependency in dependencies:
            dependency_policy = policies.get(dependency)
            if dependency_policy is None:
                errors.append(f"driver {name!r}: unknown dependency {dependency!r}")
                continue
            if entry.get("enabled") and not dependency_policy.get("enabled"):
                errors.append(f"driver {name!r}: enabled driver depends on disabled driver {dependency!r}")
            stage = entry.get("load_stage")
            dependency_stage = dependency_policy.get("load_stage")
            if stage in STAGE_RANK and dependency_stage in STAGE_RANK and STAGE_RANK[dependency_stage] > STAGE_RANK[stage]:
                errors.append(f"driver {name!r}: dependency {dependency!r} is in a later stage")
            if entry.get("load_policy") == "automatic" and dependency_policy.get("load_policy") == "manual":
                errors.append(f"driver {name!r}: automatic driver depends on manual driver {dependency!r}")

    visiting = set()
    visited = set()

    def visit(name: str, chain: list[str]) -> None:
        if name in visiting:
            start = chain.index(name) if name in chain else 0
            errors.append("driver dependency cycle: " + " -> ".join(chain[start:] + [name]))
            return
        if name in visited:
            return
        visiting.add(name)
        settings = settings_by_name.get(name, {})
        dependencies = settings.get("dependencies", []) if isinstance(settings, dict) else []
        if isinstance(dependencies, list):
            for dependency in dependencies:
                if dependency in settings_by_name:
                    visit(dependency, chain + [name])
        visiting.remove(name)
        visited.add(name)

    for name in settings_by_name:
        visit(name, [])
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--config", type=Path)
    args = parser.parse_args()
    errors = validate_policy(args.root, args.config)
    if errors:
        print("driver policy validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("driver policy validation OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
