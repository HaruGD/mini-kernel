#include "fs/vfs.h"
#include "kernel/kernel_diag.h"
#include "kernel/kutil64.h"
#include "kernel/process64.h"

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
    return "none";
}

void print_process_summary(const Process* process, uint32_t tick_now) {
    if (process == 0 || process->pid == 0) {
        print("none");
        return;
    }

    print("pid=");
    print_hex32(process->pid);
    print(" name=");
    print(process->name);
    print(" parent=");
    print_hex32(process->parent_pid);
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
    print(" mode=");
    print(process->background ? "bg" : "fg");
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
    print(" reaped=");
    print(process->reaped ? "yes" : "no");
}

void print_process_table(uint32_t tick_now) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        print("\n[");
        print_hex32(i);
        print("] ");
        print_process_summary(&process_table[i], tick_now);
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

void print_scheduler_info(Process* const* sched_queue,
                          uint32_t sched_queue_count,
                          uint32_t sched_queue_head,
                          uint32_t sched_queue_capacity,
                          uint32_t sched_last_pid,
                          uint32_t sched_switch_count,
                          uint32_t sched_yield_count,
                          uint32_t focused_pid,
                          uint32_t tick_now) {
    print("\n=== SCHEDULER ===");
    print("\nQueue count: ");
    print_hex32(sched_queue_count);
    print("\nHead: ");
    print_hex32(sched_queue_head);
    print("\nLast PID: ");
    print_hex32(sched_last_pid);
    print("\nSwitches: ");
    print_hex32(sched_switch_count);
    print("\nYields: ");
    print_hex32(sched_yield_count);
    print("\nInput focus PID: ");
    print_hex32(focused_pid);

    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % sched_queue_capacity;
        print("\nQ[");
        print_hex32(i);
        print("] ");
        print_process_summary(sched_queue[index], tick_now);
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
