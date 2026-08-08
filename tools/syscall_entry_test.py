#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>
#include <stdio.h>

#include "arch/x86_64/syscall_entry.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"
#include "kernel/mm/address_space.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/thread.h"

static CpuLocal local;
static CpuTopologyStats topology;
static Process process;
static Thread thread;
static Process* selected_process = &process;
static Thread* selected_thread = &thread;
static int allow_execute = 1;
static int allow_write = 1;
static int failures = 0;

static void check_at(int condition, int line) {
    if (!condition) {
        failures++;
        fprintf(stderr, "syscall entry check failed at line %d\n", line);
    }
}
#define check(condition) check_at((condition), __LINE__)

CpuLocal* cpu_local_current() { return &local; }
CpuLocal* cpu_local_by_id(uint32_t logical_id) {
    return logical_id == 0 ? &local : 0;
}
int cpu_local_validate(const CpuLocal* candidate) {
    return candidate == &local && candidate->self == candidate &&
           candidate->magic == CPU_LOCAL_MAGIC && candidate->prepared;
}
const CpuTopologyStats* cpu_topology_stats() { return &topology; }
Process* current_process() { return selected_process; }
Thread* current_thread() { return selected_thread; }
int user_address_is_canonical(uint64_t address) {
    return address < 0x0000800000000000ULL;
}
int address_space_address_has_rights(const AddressSpace*, uint64_t address,
                                     uint32_t rights) {
    if (address == 0x400000u && (rights & ADDRESS_SPACE_REGION_EXECUTE)) {
        return allow_execute;
    }
    if (address == 0x7FFFFFu && (rights & ADDRESS_SPACE_REGION_WRITE)) {
        return allow_write;
    }
    return 0;
}
void process_mark_failed(Process* target, uint32_t reason,
                         uint32_t status_code) {
    target->active = 0;
    target->state = PROCESS_STATE_FAILED;
    target->termination_reason = reason;
    target->status_code = status_code;
}

static void reset_context(uint64_t* frame) {
    local = {};
    topology = {};
    process = {};
    thread = {};
    local.self = &local;
    local.magic = CPU_LOCAL_MAGIC;
    local.prepared = 1;
    topology.record_count = 1;
    process.pid = 7;
    process.generation = 3;
    process.active = 1;
    process.state = PROCESS_STATE_RUNNING;
    process.address_space.identity = 0xA1u;
    process.address_space.root_phys = 0xB000u;
    thread.tid = 11;
    thread.generation = 5;
    thread.owner_pid = process.pid;
    thread.owner_generation = process.generation;
    thread.owner = &process;
    thread.active = 1;
    local.loaded_address_space = &process.address_space;
    local.loaded_address_space_identity = process.address_space.identity;
    local.loaded_address_space_root = process.address_space.root_phys;
    selected_process = &process;
    selected_thread = &thread;
    allow_execute = 1;
    allow_write = 1;
    for (uint32_t i = 0; i < 20; i++) frame[i] = 0;
    frame[15] = 0x400000u;
    frame[16] = 0x2Bu;
    frame[17] = 0x202u;
    frame[18] = 0x800000u;
    frame[19] = 0x23u;
    syscall_entry_set_kernel_stack(0x100000u);
    syscall_entry_host_set_user_active(0);
}

int main() {
    uint64_t frame[20];
    reset_context(frame);
    check(syscall_entry_init_current_cpu());
    check(syscall_entry_current_ready());

    check(syscall_fast_enter64(frame) == 1);
    check(syscall_fast_return64(frame) == SYSCALL_FAST_RETURN_SYSRET);

    syscall_entry_host_set_user_active(0);
    frame[17] |= 1u << 10;
    check(syscall_fast_enter64(frame) == 1);
    check(syscall_fast_return64(frame) == SYSCALL_FAST_RETURN_IRET);
    check((frame[17] & (1u << 10)) != 0);

    syscall_entry_host_set_user_active(0);
    frame[17] = 0x202u;
    check(syscall_fast_enter64(frame) == 1);
    process.address_space.identity++;
    check(syscall_fast_return64(frame) == SYSCALL_FAST_RETURN_ABORT);
    check(!process.active && process.state == PROCESS_STATE_FAILED);
    check(process.termination_reason == PROCESS_TERM_GP_FAULT);
    check(process.status_code == 0x5E01u);

    reset_context(frame);
    check(syscall_entry_init_current_cpu());
    allow_execute = 0;
    check(syscall_fast_enter64(frame) == 0);
    check(!process.active);

    reset_context(frame);
    check(syscall_entry_init_current_cpu());
    allow_write = 0;
    check(syscall_fast_enter64(frame) == 0);

    reset_context(frame);
    check(syscall_entry_init_current_cpu());
    check(syscall_fast_enter64(frame) == 1);
    syscall_fast_leave_to_kernel64();

    SyscallEntryDiagnostics diagnostics;
    syscall_entry_get_diagnostics(&diagnostics);
    check(diagnostics.cpu_count == 1 && diagnostics.ready_cpu_count == 1);
    check(diagnostics.fmask == SYSCALL_ENTRY_FMASK);
    return failures == 0 ? 0 : 1;
}
"""


def require_source_contracts() -> None:
    asm = (ROOT / "arch/x86_64/syscall64.asm").read_text(encoding="utf-8")
    common = (ROOT / "arch/x86_64/idt64.asm").read_text(encoding="utf-8")
    entry = (ROOT / "kernel/syscall/entry64.cpp").read_text(encoding="utf-8")
    probe = (ROOT / "user/programs/usyscall_entry_c.c").read_text(encoding="utf-8")
    requirements = {
        "entry switches to the per-CPU kernel stack": "SYSCALL_CPU_STACK_TOP" in asm,
        "entry executes swapgs": "swapgs" in asm,
        "both transports share the dispatcher frame": "syscall_frame_dispatch_asm" in common,
        "fast return has sysret": "sysret" in common,
        "fast return has iret fallback": ".fast_iret" in common,
        "FMASK is programmed": "IA32_FMASK" in entry,
        "SDK probe declares architectural clobbers": '"rcx", "r11", "memory"' in probe,
    }
    missing = [name for name, present in requirements.items() if not present]
    if missing:
        raise SystemExit("missing syscall entry contracts: " + ", ".join(missing))


def main() -> int:
    require_source_contracts()
    with tempfile.TemporaryDirectory(prefix="os64_syscall_entry_") as directory:
        temp = Path(directory)
        source = temp / "syscall_entry_test.cpp"
        binary = temp / "syscall_entry_test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/syscall/entry64.cpp"),
            str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("syscall entry contract and state test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
