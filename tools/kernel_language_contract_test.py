#!/usr/bin/env python3
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "bin/kernel64.elf"

REQUIRED_CPP_FLAGS = {
    "-ffreestanding",
    "-nostdlib",
    "-nostartfiles",
    "-nodefaultlibs",
    "-std=gnu++17",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-use-cxa-atexit",
    "-fno-threadsafe-statics",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
}
FORBIDDEN_SOURCE_PATTERNS = {
    r"\bthrow\b": "throw expression",
    r"\btry\b": "try block",
    r"\bcatch\b": "catch block",
    r"\bdynamic_cast\b": "dynamic_cast",
    r"\btypeid\b": "typeid",
    r"\bstd::": "C++ standard library reference",
}
ALLOWED_GLOBAL_INITIALIZERS = {
    "_GLOBAL__sub_I_terminal",
    "_GLOBAL__sub_I_gop",
}
ALLOWED_VTABLES = {
    "vtable for ATADriver",
    "vtable for FAT32Driver",
    "vtable for KeyboardDriver",
    "vtable for PIT",
    "vtable for Ps2MouseDriver",
}
MAX_TEXT_BYTES = 384 * 1024
MAX_DATA_BYTES = 64 * 1024
MAX_BSS_BYTES = 2 * 1024 * 1024


def command(*args: str, clean_make_env: bool = False) -> str:
    env = None
    if clean_make_env:
        env = os.environ.copy()
        env.pop("MAKEFLAGS", None)
        env.pop("MFLAGS", None)
    return subprocess.run(
        args, cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE, env=env
    ).stdout


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def make_variable(text: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}\s*=\s*(.*)$", text, re.MULTILINE)
    return match.group(1) if match else ""


def source_tokens(text: str) -> str:
    return re.sub(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        " ", text, flags=re.DOTALL)


def source_contract(failures: list[str]) -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    cpp_flags = make_variable(makefile, "HOST64_CPPFLAGS")
    driver_cpp_flags = make_variable(makefile, "DRIVER64_CPPFLAGS")
    require("KERNEL_OPT ?= -Os" in makefile,
            "the default kernel optimization profile is not explicit", failures)
    require("KERNEL_OPT_ALLOWED = -Os -Og -O2" in makefile and
            "$(KERNEL64_OBJECTS): $(KERNEL_PUBLIC_HEADERS) $(KERNEL_PROFILE_STAMP)" in makefile,
            "kernel profile validation or rebuild stamp is missing", failures)
    for flag in sorted(REQUIRED_CPP_FLAGS):
        require(flag in cpp_flags, f"kernel C++ flags missing {flag}", failures)
        require(flag in driver_cpp_flags,
                f"linked-driver C++ flags missing {flag}", failures)
    require("-O0" not in cpp_flags,
            "kernel C++ base flags contain a conflicting optimization level",
            failures)
    require("$(HOST64_CPPFLAGS) -Os" not in makefile and
            "$(HOST64_CFLAGS) -Os" not in makefile,
            "kernel recipes bypass KERNEL_OPT", failures)
    closure = re.search(r"^test-closure:\s*(.*)$", makefile, re.MULTILINE)
    require(closure is not None and
            "test-kernel-language-contract" in closure.group(1).split(),
            "kernel language contract is not part of closure", failures)

    driver_project = (ROOT / "tools/driver_project.py").read_text(
        encoding="utf-8")
    for flag in sorted(REQUIRED_CPP_FLAGS):
        require(f'"{flag}"' in driver_project,
                f"packaged C++ driver flags missing {flag}", failures)
    require(not (ROOT / "kernel/util/cpprt.cpp").exists(),
            "duplicate unlinked C++ runtime source returned", failures)

    shell = (ROOT / "kernel/shell/ksh64.cpp").read_text(encoding="utf-8")
    require("static int history_index = 0;" in shell,
            "shell history reintroduced avoidable dynamic initialization",
            failures)

    virtual_files: set[str] = set()
    roots = (ROOT / "arch", ROOT / "kernel", ROOT / "drivers", ROOT / "include")
    for base in roots:
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}:
                continue
            text = source_tokens(path.read_text(encoding="utf-8"))
            relative = path.relative_to(ROOT).as_posix()
            if re.search(r"\bvirtual\b", text):
                virtual_files.add(relative)
            for pattern, label in FORBIDDEN_SOURCE_PATTERNS.items():
                if re.search(pattern, text):
                    failures.append(f"{relative}: forbidden {label}")
    require(virtual_files == {"include/drivers/driver.h"},
            f"virtual dispatch boundary drifted: {sorted(virtual_files)}",
            failures)

    for profile in ("-Og", "-O2"):
        dry_run = command(
            "make", "-Bn", "./build/spinlock64.o", f"KERNEL_OPT={profile}",
            clean_make_env=True)
        compile_lines = [
            line for line in dry_run.splitlines() if "spinlock.cpp" in line
        ]
        require(len(compile_lines) == 1 and f" {profile} " in compile_lines[0] and
                " -Os " not in compile_lines[0] and " -O0 " not in compile_lines[0],
                f"KERNEL_OPT={profile} does not select one unambiguous optimization level",
                failures)
    make_env = os.environ.copy()
    make_env.pop("MAKEFLAGS", None)
    make_env.pop("MFLAGS", None)
    rejected_profiles = {
        "-O3": "unsupported KERNEL_OPT",
        "-Os -O2": "KERNEL_OPT must select exactly one optimization profile",
    }
    for profile, diagnostic in rejected_profiles.items():
        invalid = subprocess.run(
            ["make", "-n", "./build/spinlock64.o", f"KERNEL_OPT={profile}"],
            cwd=ROOT, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=make_env)
        require(invalid.returncode != 0 and diagnostic in invalid.stdout,
                f"invalid kernel optimization profile was not rejected: {profile}",
                failures)


def binary_contract(failures: list[str]) -> tuple[int, int, int]:
    require(ELF.exists(), "bin/kernel64.elf is missing", failures)
    if not ELF.exists():
        return 0, 0, 0

    undefined = command("nm", "-u", str(ELF)).strip()
    require(not undefined,
            f"kernel has undefined runtime symbols: {undefined}", failures)

    symbols = command("nm", "-C", str(ELF))
    raw_symbols = command("nm", str(ELF))
    initializers = {
        line.split()[-1] for line in raw_symbols.splitlines()
        if "_GLOBAL__sub_I_" in line
    }
    require(initializers == ALLOWED_GLOBAL_INITIALIZERS,
            f"global initializer allowlist drifted: {sorted(initializers)}",
            failures)
    vtables = {
        line.split(" vtable for ", 1)[1]
        for line in symbols.splitlines() if " vtable for " in line
    }
    vtables = {f"vtable for {name}" for name in vtables}
    require(vtables == ALLOWED_VTABLES,
            f"kernel vtable allowlist drifted: {sorted(vtables)}", failures)

    for token in ("__cxa_guard", "__cxa_throw", "__gxx_personality",
                  "_Unwind_", "typeinfo for ", "typeinfo name for "):
        require(token not in symbols,
                f"forbidden C++ runtime symbol present: {token}", failures)
    require(" T __cxa_pure_virtual" in symbols,
            "fail-closed pure-virtual trap is missing", failures)

    sections = command("readelf", "-SW", str(ELF))
    for section in (".eh_frame", ".gcc_except_table", ".fini_array"):
        require(section not in sections,
                f"forbidden kernel section present: {section}", failures)
    init_match = re.search(
        r"\.init_array\s+INIT_ARRAY\s+\S+\s+\S+\s+([0-9a-fA-F]+)",
        sections)
    init_size = int(init_match.group(1), 16) if init_match else 0
    require(init_size == len(ALLOWED_GLOBAL_INITIALIZERS) * 8,
            f".init_array size is {init_size}, expected 16", failures)

    size_lines = command("size", str(ELF)).splitlines()
    require(len(size_lines) >= 2, "size output is incomplete", failures)
    if len(size_lines) < 2:
        return 0, 0, 0
    fields = size_lines[1].split()
    text_bytes, data_bytes, bss_bytes = map(int, fields[:3])
    require(text_bytes <= MAX_TEXT_BYTES,
            f"kernel text budget exceeded: {text_bytes} > {MAX_TEXT_BYTES}",
            failures)
    require(data_bytes <= MAX_DATA_BYTES,
            f"kernel data budget exceeded: {data_bytes} > {MAX_DATA_BYTES}",
            failures)
    require(bss_bytes <= MAX_BSS_BYTES,
            f"kernel BSS budget exceeded: {bss_bytes} > {MAX_BSS_BYTES}",
            failures)
    return text_bytes, data_bytes, bss_bytes


def documentation_contract(failures: list[str]) -> None:
    document = ROOT / "docs/architecture/kernel_language_toolchain_contract.md"
    require(document.exists(), "kernel language contract document is missing",
            failures)
    if not document.exists():
        return
    text = document.read_text(encoding="utf-8")
    for term in ("KERNEL_OPT", "C ABI", "global initializer", "error code",
                 "test-kernel-language-contract", "384 KiB", "2 MiB"):
        require(term in text, f"kernel language contract missing {term}", failures)
    index = (ROOT / "docs/README.md").read_text(encoding="utf-8")
    require("kernel_language_toolchain_contract.md" in index,
            "documentation index does not link the kernel language contract",
            failures)


def main() -> int:
    failures: list[str] = []
    source_contract(failures)
    text_bytes, data_bytes, bss_bytes = binary_contract(failures)
    documentation_contract(failures)
    if failures:
        print("kernel language/toolchain contract failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("kernel language/toolchain contract OK")
    print(f"kernel size text={text_bytes} data={data_bytes} bss={bss_bytes}")
    print(f"global_initializers={len(ALLOWED_GLOBAL_INITIALIZERS)} "
          f"vtables={len(ALLOWED_VTABLES)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
