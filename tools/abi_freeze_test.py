#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    "display_types.h",
    "graphics_types.h",
    "handle_types.h",
    "input_types.h",
    "ipc_types.h",
    "process_types.h",
    "result.h",
    "service_types.h",
    "service_protocol_types.h",
    "service_manager_types.h",
    "surface_types.h",
    "syscall_numbers.h",
    "terminal_types.h",
    "thread_types.h",
    "window_types.h",
)


def source(cxx: bool) -> str:
    assertions = (
        "static_assert" if cxx else "_Static_assert"
    )
    includes = "\n".join(f'#include "os64/{header}"' for header in HEADERS)
    return f"""{includes}
{assertions}(OS64_GRAPHICS_ABI_VERSION == 1u, "graphics ABI version changed");
{assertions}(OS64_DISPLAY_ABI_VERSION == 1u, "display ABI version changed");
{assertions}(OS64_HANDLE_ABI_VERSION == 1u, "handle ABI version changed");
{assertions}(OS64_INPUT_ABI_VERSION == 1u, "input ABI version changed");
{assertions}(OS64_IPC_ABI_VERSION_V1 == 1u, "IPC v1 ABI version changed");
{assertions}(OS64_IPC_ABI_VERSION_V2 == 2u, "IPC v2 ABI version changed");
{assertions}(OS64_PROCESS_ABI_VERSION == 1u, "process ABI version changed");
{assertions}(OS64_SERVICE_ABI_VERSION == 1u, "service ABI version changed");
{assertions}(OS64_SERVICE_PROTOCOL_ABI_VERSION == 2u, "service protocol ABI version changed");
{assertions}(OS64_SERVICE_MANAGER_ABI_VERSION == 2u, "service manager ABI version changed");
{assertions}(OS64_SURFACE_ABI_VERSION == 1u, "surface ABI version changed");
{assertions}(OS64_TERMINAL_ABI_VERSION == 1u, "terminal ABI version changed");
{assertions}(OS64_THREAD_ABI_VERSION == 2u, "thread ABI version changed");
{assertions}(OS64_WINDOW_ABI_VERSION == 1u, "window ABI version changed");
{assertions}(OS64_SYSCALL_CATALOG_VERSION == 3u, "syscall catalog version changed");
{assertions}(OS64_SYSCALL_MAX_NUMBER == 111u, "syscall number range changed");
{assertions}(SYS_WRITE == 1u, "first syscall number changed");
{assertions}(SYS_TERMINAL_SESSION_CLOSE == 111u, "last syscall number changed");
{assertions}(OS_ERR_CANCELLED == -18, "syscall result ABI changed");
{assertions}(sizeof(OsResult) == 8u, "syscall result width changed");
{assertions}(OS_ERR_INVALID_HANDLE == -19, "invalid-handle result changed");
{assertions}(OS_ERR_STALE_HANDLE == -20, "stale-handle result changed");
{assertions}(OS_ERR_WRONG_HANDLE_TYPE == -21, "handle-type result changed");
{assertions}(OS_ERR_OVERFLOW == -22, "overflow result changed");
{assertions}(OS64_RESULT_ERROR_MIN == -4095, "public result range changed");
{assertions}(OS_SURFACE_MAP_VALID_MASK == 3u, "surface map flags changed");
{assertions}(OS_SURFACE_TRANSFER_RIGHTS == (OS_HANDLE_RIGHT_READ | OS_HANDLE_RIGHT_MAP),
             "surface transfer rights changed");
int main(void) {{ return 0; }}
"""


def compile_header_view(include_dir: Path, cxx: bool, temp: Path) -> None:
    suffix = "cpp" if cxx else "c"
    path = temp / f"abi_{include_dir.name}_{suffix}.{suffix}"
    path.write_text(source(cxx), encoding="utf-8")
    compiler = "g++" if cxx else "gcc"
    standard = "-std=c++17" if cxx else "-std=c11"
    subprocess.run([
        compiler, standard, "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-I", str(include_dir), str(path),
    ], check=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_abi_freeze_") as directory:
        temp = Path(directory)
        for include_dir in (ROOT / "include", ROOT / "user/sdk/include"):
            compile_header_view(include_dir, False, temp)
            compile_header_view(include_dir, True, temp)
    print("ABI freeze test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
