#include "fs/vfs.h"
#include "kernel/kernel_diag.h"
#include "kernel/kutil64.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"
#include "kernel/spinlock.h"

const char* process_state_name(uint32_t state) {
    if (state == PROCESS_STATE_LOADED) {
        return "loaded";
    }
    if (state == PROCESS_STATE_RUNNING) {
        return "running";
    }
    if (state == PROCESS_STATE_RETURNED) {
        return "returned";
    }
    if (state == PROCESS_STATE_FAILED) {
        return "failed";
    }
    if (state == PROCESS_STATE_PAUSED) {
        return "paused";
    }
    return "empty";
}

const char* process_term_name(uint32_t reason) {
    if (reason == PROCESS_TERM_EXIT) {
        return "exit";
    }
    if (reason == PROCESS_TERM_LOAD_ERROR) {
        return "load_error";
    }
    if (reason == PROCESS_TERM_READ_ERROR) {
        return "read_error";
    }
    if (reason == PROCESS_TERM_MEMORY_ERROR) {
        return "memory_error";
    }
    if (reason == PROCESS_TERM_MAP_ERROR) {
        return "map_error";
    }
    if (reason == PROCESS_TERM_PAGE_FAULT) {
        return "page_fault";
    }
    if (reason == PROCESS_TERM_GP_FAULT) {
        return "gp_fault";
    }
    if (reason == PROCESS_TERM_DOUBLE_FAULT) {
        return "double_fault";
    }
    if (reason == PROCESS_TERM_KILLED) {
        return "killed";
    }
    return "none";
}

const char* scheduler_state_name(uint32_t state) {
    if (state == SCHED_STATE_READY) {
        return "ready";
    }
    if (state == SCHED_STATE_RUNNING) {
        return "running";
    }
    if (state == SCHED_STATE_WAITING) {
        return "waiting";
    }
    if (state == SCHED_STATE_FINISHED) {
        return "finished";
    }
    return "none";
}

const char* pause_reason_name(uint32_t reason) {
    if (reason == PROCESS_PAUSE_YIELD) {
        return "yield";
    }
    if (reason == PROCESS_PAUSE_PREEMPT) {
        return "preempt";
    }
    if (reason == PROCESS_PAUSE_SLEEP) {
        return "sleep";
    }
    if (reason == PROCESS_PAUSE_WAIT) {
        return "wait";
    }
    return "none";
}

template <typename ProcessView>
static void print_process_summary_view(const ProcessView* process, uint32_t tick_now) {
    if (process == 0 || process->pid == 0) {
        print("none");
        return;
    }

    print("pid=");
    print_hex32(process->pid);
    print(" gen=");
    print_hex32(process->generation);
    print(" name=");
    print(process->name);
    print(" parent=");
    print_hex32(process->parent_pid);
    print(" parent_gen=");
    print_hex32(process->parent_generation);
    print(" slot=");
    print_hex32(process->slot_index);
    print(" state=");
    print(process_state_name(process->state));
    print(" term=");
    print(process_term_name(process->termination_reason));
    print(" code=");
    print_hex32(process->status_code);
    print(" sched=");
    print(scheduler_state_name(process->scheduler_state));
    print(" pause=");
    print(pause_reason_name(process->pause_reason));
    print(" wait=");
    print(process_wait_reason_name(process->wait_reason));
    print(" mode=");
    print(process->background ? "bg" : "fg");
    print(" perms=");
    print_hex32(process->permissions);
    print(" ticks=");
    print_hex32(process->runtime_ticks);
    print(" slice=");
    print_hex32(process->timeslice_ticks);
    if (process->scheduler_state == SCHED_STATE_WAITING && process->pause_reason == PROCESS_PAUSE_SLEEP) {
        uint32_t remaining = process->wake_tick > tick_now ? (process->wake_tick - tick_now) : 0;
        print(" wake=");
        print_hex32(process->wake_tick);
        print(" remain=");
        print_hex32(remaining);
    }
    if (process->wait_has_deadline && process->wait_reason != PROCESS_WAIT_TIMER) {
        uint32_t remaining = (int32_t)(process->wait_deadline - tick_now) > 0
            ? process->wait_deadline - tick_now
            : 0;
        print(" timeout=");
        print_hex32(remaining);
    }
    print(" reaped=");
    print(process->reaped ? "yes" : "no");
}

void print_process_summary(const Process* process, uint32_t tick_now) {
    print_process_summary_view(process, tick_now);
}

void print_process_table(uint32_t tick_now) {
    SchedulerDiagnosticSnapshot snapshot;
    process_get_diagnostic_snapshot(&snapshot);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        print("\n[");
        print_hex32(i);
        print("] ");
        print_process_summary_view(&snapshot.processes[i], tick_now);
    }
    print("\n");
}

void print_job_compact(const char* label, const Process* process, uint32_t tick_now) {
    if (process == 0 || process->pid == 0) {
        print("\n");
        print(label);
        print(": none");
        return;
    }

    print("\n");
    print(label);
    print(": pid=");
    print_hex32(process->pid);
    print(" gen=");
    print_hex32(process->generation);
    print(" ");
    print(process_state_name(process->state));
    print(" ");
    print(process->background ? "bg" : "fg");

    if (process->scheduler_state != SCHED_STATE_RUNNING) {
        print(" ");
        print(scheduler_state_name(process->scheduler_state));
    }

    if (process->pause_reason != PROCESS_PAUSE_NONE) {
        print("/");
        print(pause_reason_name(process->pause_reason));
    }

    if (process->termination_reason != PROCESS_TERM_NONE) {
        print(" ");
        print(process_term_name(process->termination_reason));
        if (process->termination_reason == PROCESS_TERM_EXIT ||
            process->termination_reason == PROCESS_TERM_PAGE_FAULT ||
            process->termination_reason == PROCESS_TERM_LOAD_ERROR ||
            process->termination_reason == PROCESS_TERM_KILLED) {
            print(" code=");
            print_hex32(process->status_code);
        }
    }

    if (process->scheduler_state == SCHED_STATE_WAITING &&
        process->pause_reason == PROCESS_PAUSE_SLEEP) {
        uint32_t remaining = process->wake_tick > tick_now ? (process->wake_tick - tick_now) : 0;
        print(" remain=");
        print_hex32(remaining);
    }

    print(" ");
    print(process->name);
}

void print_child_result_compact(const char* label, const Process* process) {
    if (process == 0 || process->pid == 0) {
        print("\n");
        print(label);
        print(": none");
        return;
    }

    print("\n");
    print(label);
    print(": pid=");
    print_hex32(process->pid);
    print(" gen=");
    print_hex32(process->generation);
    print(" ");
    print(process_state_name(process->state));

    if (process->termination_reason != PROCESS_TERM_NONE) {
        print(" ");
        print(process_term_name(process->termination_reason));
    }

    print(" code=");
    print_hex32(process->status_code);

    if (process->reaped) {
        print(" reaped");
    }

    print(" ");
    print(process->name);
}

void print_jobs_for_process(const Process* parent, uint32_t tick_now) {
    print("\n=== JOBS ===");
    if (parent == 0) {
        print("\nNo current user process.");
        print("\n============");
        return;
    }

    print_job_compact("self", parent, tick_now);

    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent->pid) {
            continue;
        }
        char label[16];
        label[0] = 'j';
        label[1] = 'o';
        label[2] = 'b';
        label[3] = '[';
        label[4] = (char)('0' + (count % 10));
        label[5] = ']';
        label[6] = '\0';
        print_job_compact(label, process, tick_now);
        count++;
    }

    if (count == 0) {
        print("\n(no child jobs)");
    }
    print("\n(use ps for full details)");
    print("\n============\n");
}

void print_ipc_info() {
    SchedulerDiagnosticSnapshot snapshot;
    process_get_diagnostic_snapshot(&snapshot);
    print("\n=== IPC ===");
    print("\nmailbox_capacity=");
    print_hex32(IPC_MAILBOX_CAPACITY);

    uint32_t active_count = 0;
    for (uint32_t i = 0; i < snapshot.process_count; i++) {
        const ProcessDiagnosticSnapshot* process = &snapshot.processes[i];
        if (process->pid == 0) {
            continue;
        }
        active_count++;
        print("\n[");
        print_hex32(i);
        print("] pid=");
        print_hex32(process->pid);
        print(" gen=");
        print_hex32(process->generation);
        print(" name=");
        print(process->name);
        print(" state=");
        print(process_state_name(process->state));
        print(" wait=");
        print_hex32(process->wait_pending && process->wait_reason == PROCESS_WAIT_IPC ? 1 : 0);
        print(" queued=");
        print_hex32(process->mailbox.count);
        print(" delivered=");
        print_hex32(process->mailbox.delivered_count);
        print(" dropped=");
        print_hex32(process->mailbox.dropped_count);
        print(" handles=");
        print_hex32(process->handle_count);
    }

    if (active_count == 0) {
        print("\n(no processes)");
    }
    print("\n===========");
}

void print_service_registry() {
    ServiceRegistrySnapshot snapshot;
    service_registry_get_snapshot(&snapshot);
    print("\n=== SERVICES ===");
    print("\ncapacity=");
    print_hex32(snapshot.capacity);
    print(" count=");
    print_hex32(snapshot.count);

    for (uint32_t i = 0; i < snapshot.count; i++) {
        const OsServiceInfo* info = &snapshot.entries[i];
        print("\n[");
        print_hex32(i);
        print("] name=");
        print(info->name);
        print(" pid=");
        print_hex32(info->owner_pid);
        print(" state=");
        print(service_state_name(info->state));
        print(" flags=");
        print_hex32(info->flags);
        print(" generation=");
        print_hex32(info->generation);
    }

    if (snapshot.count == 0) {
        print("\n(no services)");
    }
    print("\n================");
}

void print_concurrency_info() {
    KernelSpinlockStats stats;
    kernel_spinlock_get_stats(&stats);
    print("\n=== CONCURRENCY ===");
    print("\nacquisitions=");
    print_hex64(stats.acquisitions);
    print(" contentions=");
    print_hex64(stats.contentions);
    print("\norder_violations=");
    print_hex64(stats.order_violations);
    print(" recursion_violations=");
    print_hex64(stats.recursion_violations);
    print(" release_violations=");
    print_hex64(stats.release_violations);
    print("\ndepth=");
    print_hex32(stats.current_depth);
    print(" max_depth=");
    print_hex32(stats.maximum_depth);
    print(" class=");
    print_hex32(stats.current_class);
    print(" interrupts=");
    print(stats.interrupts_enabled ? "enabled" : "disabled");
    print("\nlast_violation=");
    print_hex32(stats.last_violation_type);
    print(" held_class=");
    print_hex32(stats.last_held_class);
    print(" requested_class=");
    print_hex32(stats.last_requested_class);
    print("\n===================");
}

void print_scheduler_info(Process* const* sched_queue,
                          uint32_t sched_queue_count,
                          uint32_t sched_queue_head,
                          uint32_t sched_queue_capacity,
                          uint32_t sched_last_pid,
                          uint32_t sched_switch_count,
                          uint32_t sched_yield_count,
                          uint32_t focused_pid,
                          uint32_t tick_now) {
    (void)sched_queue;
    (void)sched_queue_count;
    (void)sched_queue_head;
    (void)sched_queue_capacity;
    (void)sched_last_pid;
    (void)sched_switch_count;
    (void)sched_yield_count;
    (void)focused_pid;
    (void)tick_now;
    SchedulerDiagnosticSnapshot snapshot;
    process_get_diagnostic_snapshot(&snapshot);
    print("\n=== SCHEDULER ===");
    print("\nQueue count: ");
    print_hex32(snapshot.queue_count);
    print("\nHead: ");
    print_hex32(snapshot.queue_head);
    print("\nLast PID: ");
    print_hex32(snapshot.last_pid);
    print("\nSwitches: ");
    print_hex32(snapshot.switch_count);
    print("\nYields: ");
    print_hex32(snapshot.yield_count);
    print("\nInput focus PID: ");
    print_hex32(snapshot.focused_pid);

    for (uint32_t i = 0; i < snapshot.queue_count; i++) {
        uint32_t index = (snapshot.queue_head + i) % SCHED_QUEUE_SIZE;
        uint32_t pid = snapshot.queue_pids[index];
        print("\nQ[");
        print_hex32(i);
        print("] pid=");
        print_hex32(pid);
        for (uint32_t p = 0; p < snapshot.process_count; p++) {
            if (snapshot.processes[p].pid != pid) {
                continue;
            }
            print(" name=");
            print(snapshot.processes[p].name);
            print(" state=");
            print(process_state_name(snapshot.processes[p].state));
            print(" sched=");
            print(scheduler_state_name(snapshot.processes[p].scheduler_state));
            print(" ticks=");
            print_hex32(snapshot.processes[p].runtime_ticks);
            break;
        }
    }
    print("\n=================\n");
}

void print_vfs_mounts() {
    print("\n=== VFS MOUNTS ===");

    uint32_t count = vfs_mount_count();
    if (count == 0) {
        print("\n(no mounts)");
        print("\n==================\n");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        VFSMountInfo info;
        if (vfs_get_mount_info(i, &info) != VFS_OK) {
            continue;
        }

        print("\nmount[");
        print_hex32(i);
        print("] ");
        print(info.mount_path);
        print(" fs=");
        print(info.fs_name);
        print(" backend=");
        print_hex32(info.backend_kind);
    }

    print("\n==================\n");
}
