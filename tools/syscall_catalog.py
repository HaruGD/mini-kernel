#!/usr/bin/env python3
"""Load and validate the versioned OS64 system-call catalog."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "config/abi/syscalls.json"
SCHEMA_PATH = ROOT / "config/schemas/syscalls.schema.json"

ROOT_FIELDS = {
    "schema_version", "catalog_version", "abi", "result_domain", "defaults",
    "error_codes", "error_sets", "syscalls",
}
ABI_FIELDS = {
    "name", "architecture", "number_register", "result_register",
    "argument_registers", "max_number", "active_transport",
    "compatibility_transport",
}
DEFAULT_FIELDS = {"permission", "execution", "resources"}
RESULT_DOMAIN_FIELDS = {
    "width_bits", "success_min", "success_max", "error_min", "error_max",
    "unknown_syscall", "internal_control_policy",
}
ERROR_FIELDS = {
    "symbol", "kernel_symbol", "value", "category", "message", "retryable",
    "description",
}
SYSCALL_FIELDS = {
    "number", "symbol", "sdk_symbol", "name", "state", "audit_status",
    "handler", "arguments", "result", "permission", "execution",
    "resources", "reference",
}
ARGUMENT_FIELDS = {
    "name", "register", "kind", "direction", "type", "nullable", "size",
}
RESULT_FIELDS = {"kind", "success_domain", "success", "output", "errors"}
OUTPUT_FIELDS = {"publication", "failure_state", "partial_errors", "resume"}
EXECUTION_FIELDS = {"context", "blocking", "cancellation"}
RESOURCE_FIELDS = {"input_ownership", "output_ownership", "cleanup", "limit"}

SYMBOL_RE = re.compile(r"^SYS_[A-Z][A-Z0-9_]*$")
SDK_SYMBOL_RE = re.compile(r"^OS_SYS_[A-Z][A-Z0-9_]*$")
ERROR_RE = re.compile(r"^OS_ERR_[A-Z][A-Z0-9_]*$")
KERNEL_ERROR_RE = re.compile(r"^SYS_ERR_[A-Z][A-Z0-9_]*$")
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
ARGUMENT_KINDS = {"scalar", "cstring", "buffer", "structure", "handle", "address"}
ARGUMENT_DIRECTIONS = {"value", "in", "out", "inout"}
SYSCALL_STATES = {"active", "reserved", "retired"}
AUDIT_STATES = {"provisional", "audited"}
HANDLERS = {"core", "vfs", "sdk"}
CANCELLATION = {"none", "timeout", "cancellable", "process_exit"}
RESULT_KINDS = {"status", "count", "value", "handle", "address", "noreturn"}
SUCCESS_DOMAINS = {"zero", "nonnegative", "positive", "noreturn"}
OUTPUT_PUBLICATION = {"none", "atomic", "partial"}
OUTPUT_FAILURE_STATES = {"not_applicable", "unchanged", "partial"}
OUTPUT_RESUME = {"not_applicable", "retry_entire_call"}
RESULT_KIND_DOMAINS = {
    "status": "zero",
    "count": "nonnegative",
    "value": "nonnegative",
    "handle": "positive",
    "address": "positive",
    "noreturn": "noreturn",
}


class DuplicateKeyError(ValueError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_json(path: Path) -> Any:
    return json.loads(
        path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
    )


def exact_fields(value: Any, fields: set[str], label: str, errors: list[str],
                 optional: set[str] | None = None) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label}: expected an object")
        return
    optional = optional or set()
    missing = fields - optional - set(value)
    unknown = set(value) - fields
    for field in sorted(missing):
        errors.append(f"{label}: missing field {field!r}")
    for field in sorted(unknown):
        errors.append(f"{label}: unknown field {field!r}")


def nonempty_string(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{label}: expected a non-empty string")


def validate_execution(value: Any, label: str, errors: list[str]) -> None:
    exact_fields(value, EXECUTION_FIELDS, label, errors)
    if not isinstance(value, dict):
        return
    if value.get("context") != "process":
        errors.append(f"{label}.context: expected 'process'")
    if not isinstance(value.get("blocking"), bool):
        errors.append(f"{label}.blocking: expected boolean")
    if value.get("cancellation") not in CANCELLATION:
        errors.append(f"{label}.cancellation: unknown policy")


def validate_resources(value: Any, label: str, errors: list[str]) -> None:
    exact_fields(value, RESOURCE_FIELDS, label, errors)
    if not isinstance(value, dict):
        return
    for field in sorted(RESOURCE_FIELDS):
        nonempty_string(value.get(field), f"{label}.{field}", errors)


def resolve_contract(call: dict[str, Any], defaults: dict[str, Any]) -> dict[str, Any]:
    resolved = dict(call)
    for field in DEFAULT_FIELDS:
        if field not in resolved:
            resolved[field] = defaults[field]
    return resolved


def validate_catalog(catalog: Any, root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    exact_fields(catalog, ROOT_FIELDS, "catalog", errors)
    if not isinstance(catalog, dict):
        return errors
    if type(catalog.get("schema_version")) is not int or catalog.get("schema_version") != 2:
        errors.append("catalog.schema_version: expected 2")
    if type(catalog.get("catalog_version")) is not int or catalog.get("catalog_version", 0) < 1:
        errors.append("catalog.catalog_version: expected a positive integer")

    abi = catalog.get("abi")
    exact_fields(abi, ABI_FIELDS, "catalog.abi", errors)
    if isinstance(abi, dict):
        if abi.get("architecture") != "x86_64":
            errors.append("catalog.abi.architecture: expected 'x86_64'")
        if abi.get("number_register") != "rax" or abi.get("result_register") != "rax":
            errors.append("catalog.abi: number and result registers must be rax")
        registers = abi.get("argument_registers")
        if registers != ["rdi", "rsi", "rdx"]:
            errors.append("catalog.abi.argument_registers: expected rdi, rsi, rdx")
        if type(abi.get("max_number")) is not int or abi.get("max_number", 0) < 1:
            errors.append("catalog.abi.max_number: expected a positive integer")
        for field in ("name", "active_transport", "compatibility_transport"):
            nonempty_string(abi.get(field), f"catalog.abi.{field}", errors)

    result_domain = catalog.get("result_domain")
    exact_fields(result_domain, RESULT_DOMAIN_FIELDS, "catalog.result_domain", errors)
    if isinstance(result_domain, dict):
        if result_domain.get("width_bits") != 64:
            errors.append("catalog.result_domain.width_bits: expected 64")
        if result_domain.get("success_min") != 0 or \
                result_domain.get("success_max") != 0x7FFFFFFFFFFFFFFF:
            errors.append("catalog.result_domain: expected signed 64-bit nonnegative success range")
        if type(result_domain.get("error_min")) is not int or \
                result_domain.get("error_min", 0) >= 0 or \
                result_domain.get("error_max") != -1:
            errors.append("catalog.result_domain: expected a negative error range ending at -1")
        if result_domain.get("internal_control_policy") != \
                "outside_public_domain_never_returned":
            errors.append("catalog.result_domain.internal_control_policy: unknown policy")

    defaults = catalog.get("defaults")
    exact_fields(defaults, DEFAULT_FIELDS, "catalog.defaults", errors)
    if isinstance(defaults, dict):
        nonempty_string(defaults.get("permission"), "catalog.defaults.permission", errors)
        validate_execution(defaults.get("execution"), "catalog.defaults.execution", errors)
        validate_resources(defaults.get("resources"), "catalog.defaults.resources", errors)
    else:
        defaults = {}

    codes = catalog.get("error_codes")
    code_symbols: set[str] = set()
    kernel_symbols: set[str] = set()
    values: set[int] = set()
    if not isinstance(codes, list) or not codes:
        errors.append("catalog.error_codes: expected a non-empty array")
        codes = []
    for index, code in enumerate(codes):
        label = f"catalog.error_codes[{index}]"
        exact_fields(code, ERROR_FIELDS, label, errors)
        if not isinstance(code, dict):
            continue
        symbol = code.get("symbol")
        kernel_symbol = code.get("kernel_symbol")
        value = code.get("value")
        if not isinstance(symbol, str) or ERROR_RE.fullmatch(symbol) is None:
            errors.append(f"{label}.symbol: invalid error symbol")
        elif symbol in code_symbols:
            errors.append(f"{label}.symbol: duplicate {symbol}")
        else:
            code_symbols.add(symbol)
        if not isinstance(kernel_symbol, str) or KERNEL_ERROR_RE.fullmatch(kernel_symbol) is None:
            errors.append(f"{label}.kernel_symbol: invalid kernel error symbol")
        elif kernel_symbol in kernel_symbols:
            errors.append(f"{label}.kernel_symbol: duplicate {kernel_symbol}")
        else:
            kernel_symbols.add(kernel_symbol)
        if type(value) is not int or value >= 0:
            errors.append(f"{label}.value: expected a negative integer")
        elif value in values:
            errors.append(f"{label}.value: duplicate {value}")
        else:
            values.add(value)
        if not isinstance(code.get("category"), str) or \
                NAME_RE.fullmatch(code.get("category", "")) is None:
            errors.append(f"{label}.category: invalid category")
        nonempty_string(code.get("message"), f"{label}.message", errors)
        if not isinstance(code.get("retryable"), bool):
            errors.append(f"{label}.retryable: expected boolean")
        nonempty_string(code.get("description"), f"{label}.description", errors)

    if isinstance(result_domain, dict):
        error_min = result_domain.get("error_min")
        error_max = result_domain.get("error_max")
        if type(error_min) is int and type(error_max) is int:
            for value in values:
                if value < error_min or value > error_max:
                    errors.append(f"catalog.error_codes: value {value} lies outside public error range")
        if values and values != set(range(-len(values), 0)):
            errors.append("catalog.error_codes: values must be contiguous from -1")
        if result_domain.get("unknown_syscall") not in code_symbols:
            errors.append("catalog.result_domain.unknown_syscall: unknown error symbol")

    error_sets = catalog.get("error_sets")
    if not isinstance(error_sets, dict) or not error_sets:
        errors.append("catalog.error_sets: expected a non-empty object")
        error_sets = {}
    for name, members in error_sets.items():
        if NAME_RE.fullmatch(name) is None:
            errors.append(f"catalog.error_sets: invalid name {name!r}")
        if not isinstance(members, list):
            errors.append(f"catalog.error_sets.{name}: expected an array")
            continue
        seen: set[str] = set()
        for member in members:
            if member not in code_symbols:
                errors.append(f"catalog.error_sets.{name}: unknown error {member!r}")
            if member in seen:
                errors.append(f"catalog.error_sets.{name}: duplicate error {member!r}")
            seen.add(member)

    calls = catalog.get("syscalls")
    if not isinstance(calls, list) or not calls:
        errors.append("catalog.syscalls: expected a non-empty array")
        return errors
    numbers: set[int] = set()
    symbols: set[str] = set()
    sdk_symbols: set[str] = set()
    names: set[str] = set()
    argument_registers = abi.get("argument_registers", []) if isinstance(abi, dict) else []
    for index, call in enumerate(calls):
        label = f"catalog.syscalls[{index}]"
        exact_fields(call, SYSCALL_FIELDS, label, errors, optional=DEFAULT_FIELDS)
        if not isinstance(call, dict):
            continue
        number = call.get("number")
        symbol = call.get("symbol")
        sdk_symbol = call.get("sdk_symbol")
        name = call.get("name")
        if type(number) is not int or number < 1:
            errors.append(f"{label}.number: expected a positive integer")
        elif number in numbers:
            errors.append(f"{label}.number: duplicate {number}")
        else:
            numbers.add(number)
        if not isinstance(symbol, str) or SYMBOL_RE.fullmatch(symbol) is None:
            errors.append(f"{label}.symbol: invalid symbol")
        elif symbol in symbols:
            errors.append(f"{label}.symbol: duplicate {symbol}")
        else:
            symbols.add(symbol)
        if not isinstance(sdk_symbol, str) or SDK_SYMBOL_RE.fullmatch(sdk_symbol) is None:
            errors.append(f"{label}.sdk_symbol: invalid SDK symbol")
        elif sdk_symbol in sdk_symbols:
            errors.append(f"{label}.sdk_symbol: duplicate {sdk_symbol}")
        else:
            sdk_symbols.add(sdk_symbol)
        if not isinstance(name, str) or NAME_RE.fullmatch(name) is None:
            errors.append(f"{label}.name: invalid name")
        elif name in names:
            errors.append(f"{label}.name: duplicate {name}")
        else:
            names.add(name)
        if call.get("state") not in SYSCALL_STATES:
            errors.append(f"{label}.state: unknown state")
        if call.get("audit_status") not in AUDIT_STATES:
            errors.append(f"{label}.audit_status: unknown state")
        if call.get("handler") not in HANDLERS:
            errors.append(f"{label}.handler: unknown handler")

        arguments = call.get("arguments")
        if not isinstance(arguments, list) or len(arguments) > len(argument_registers):
            errors.append(f"{label}.arguments: expected at most three arguments")
            arguments = []
        argument_names: set[str] = set()
        for arg_index, argument in enumerate(arguments):
            arg_label = f"{label}.arguments[{arg_index}]"
            exact_fields(argument, ARGUMENT_FIELDS, arg_label, errors)
            if not isinstance(argument, dict):
                continue
            arg_name = argument.get("name")
            if not isinstance(arg_name, str) or NAME_RE.fullmatch(arg_name) is None:
                errors.append(f"{arg_label}.name: invalid name")
            elif arg_name in argument_names:
                errors.append(f"{arg_label}.name: duplicate {arg_name}")
            else:
                argument_names.add(arg_name)
            expected_register = argument_registers[arg_index] if arg_index < len(argument_registers) else None
            if argument.get("register") != expected_register:
                errors.append(f"{arg_label}.register: expected {expected_register}")
            kind = argument.get("kind")
            direction = argument.get("direction")
            if kind not in ARGUMENT_KINDS:
                errors.append(f"{arg_label}.kind: unknown kind")
            if direction not in ARGUMENT_DIRECTIONS:
                errors.append(f"{arg_label}.direction: unknown direction")
            if kind in {"scalar", "handle", "address"} and direction != "value":
                errors.append(f"{arg_label}: scalar/handle/address direction must be value")
            if kind in {"cstring", "buffer", "structure"} and direction == "value":
                errors.append(f"{arg_label}: pointer direction cannot be value")
            if not isinstance(argument.get("nullable"), bool):
                errors.append(f"{arg_label}.nullable: expected boolean")
            nonempty_string(argument.get("type"), f"{arg_label}.type", errors)
            nonempty_string(argument.get("size"), f"{arg_label}.size", errors)

        result = call.get("result")
        exact_fields(result, RESULT_FIELDS, f"{label}.result", errors)
        if isinstance(result, dict):
            kind = result.get("kind")
            success_domain = result.get("success_domain")
            if kind not in RESULT_KINDS:
                errors.append(f"{label}.result.kind: unknown result kind")
            if success_domain not in SUCCESS_DOMAINS:
                errors.append(f"{label}.result.success_domain: unknown domain")
            elif kind in RESULT_KIND_DOMAINS and success_domain != RESULT_KIND_DOMAINS[kind]:
                errors.append(
                    f"{label}.result.success_domain: expected {RESULT_KIND_DOMAINS[kind]} for {kind}"
                )
            nonempty_string(result.get("success"), f"{label}.result.success", errors)
            if result.get("errors") not in error_sets:
                errors.append(f"{label}.result.errors: unknown error set")
            output = result.get("output")
            output_label = f"{label}.result.output"
            exact_fields(output, OUTPUT_FIELDS, output_label, errors)
            if isinstance(output, dict):
                publication = output.get("publication")
                failure_state = output.get("failure_state")
                resume = output.get("resume")
                partial_errors = output.get("partial_errors")
                if publication not in OUTPUT_PUBLICATION:
                    errors.append(f"{output_label}.publication: unknown policy")
                if failure_state not in OUTPUT_FAILURE_STATES:
                    errors.append(f"{output_label}.failure_state: unknown policy")
                if resume not in OUTPUT_RESUME:
                    errors.append(f"{output_label}.resume: unknown policy")
                if not isinstance(partial_errors, list):
                    errors.append(f"{output_label}.partial_errors: expected an array")
                    partial_errors = []
                if len(partial_errors) != len(set(partial_errors)):
                    errors.append(f"{output_label}.partial_errors: duplicate error")
                call_errors = set(error_sets.get(result.get("errors"), []))
                for partial_error in partial_errors:
                    if partial_error not in code_symbols:
                        errors.append(f"{output_label}.partial_errors: unknown error {partial_error!r}")
                    elif partial_error not in call_errors:
                        errors.append(f"{output_label}.partial_errors: {partial_error} is outside the call error set")

                has_output = any(
                    isinstance(argument, dict) and
                    argument.get("direction") in {"out", "inout"}
                    for argument in arguments
                )
                if not has_output:
                    if publication != "none" or failure_state != "not_applicable" or \
                            partial_errors or resume != "not_applicable":
                        errors.append(f"{output_label}: calls without output pointers must use the no-output contract")
                elif publication == "none":
                    errors.append(f"{output_label}: output pointer requires a publication policy")
                elif publication == "atomic":
                    if failure_state != "unchanged" or partial_errors or \
                            resume != "retry_entire_call":
                        errors.append(f"{output_label}: atomic output must remain unchanged and retry as a whole")
                elif publication == "partial":
                    if failure_state != "partial" or not partial_errors or \
                            resume != "retry_entire_call":
                        errors.append(f"{output_label}: partial output requires named errors and whole-call retry")

        resolved = resolve_contract(call, defaults) if DEFAULT_FIELDS <= set(defaults) else call
        nonempty_string(resolved.get("permission"), f"{label}.permission", errors)
        validate_execution(resolved.get("execution"), f"{label}.execution", errors)
        validate_resources(resolved.get("resources"), f"{label}.resources", errors)
        reference = call.get("reference")
        if not isinstance(reference, str) or not reference.startswith("docs/"):
            errors.append(f"{label}.reference: expected a docs/ path")
        elif not (root / reference).is_file():
            errors.append(f"{label}.reference: missing {reference}")

    max_number = abi.get("max_number") if isinstance(abi, dict) else None
    if type(max_number) is int:
        expected = set(range(1, max_number + 1))
        if numbers != expected:
            missing = sorted(expected - numbers)
            extra = sorted(numbers - expected)
            errors.append(f"catalog.syscalls: number coverage mismatch; missing={missing}, extra={extra}")
    if isinstance(calls, list) and [c.get("number") for c in calls if isinstance(c, dict)] != sorted(numbers):
        errors.append("catalog.syscalls: entries must be sorted by number")
    return errors


def load_catalog(path: Path = CATALOG_PATH, root: Path = ROOT) -> dict[str, Any]:
    catalog = load_json(path)
    errors = validate_catalog(catalog, root)
    if errors:
        raise ValueError("invalid syscall catalog:\n- " + "\n- ".join(errors))
    return catalog


if __name__ == "__main__":
    try:
        loaded = load_catalog()
    except (OSError, json.JSONDecodeError, DuplicateKeyError, ValueError) as error:
        raise SystemExit(str(error))
    print(f"syscall catalog OK ({len(loaded['syscalls'])} calls)")
