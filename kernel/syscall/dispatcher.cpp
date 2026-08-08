#include "kernel/syscall/dispatcher.h"

#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall/user_memory.h"
#include "kernel/syscall64.h"
#include "kernel/thread.h"
#include "os64/process_types.h"

static volatile uint64_t total_calls = 0;
static volatile uint64_t dispatched_calls = 0;
static volatile uint64_t rejected_calls = 0;
static volatile uint64_t rejected_by_reason[SYSCALL_REJECT_REASON_COUNT] = {};
static volatile uint64_t last_rejected_number = 0;
static volatile uint32_t last_rejected_reason = SYSCALL_REJECT_NONE;

static void set_reason(uint32_t reason, uint32_t* output) {
    if (output != 0) {
        *output = reason;
    }
}

static void record_rejection(uint64_t number, uint32_t reason) {
    if (reason == SYSCALL_REJECT_NONE ||
        reason >= SYSCALL_REJECT_REASON_COUNT) {
        reason = SYSCALL_REJECT_EXECUTION_CONTEXT;
    }
    __atomic_add_fetch(&rejected_calls, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&rejected_by_reason[reason], 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&last_rejected_number, number, __ATOMIC_RELAXED);
    __atomic_store_n(&last_rejected_reason, reason, __ATOMIC_RELEASE);
}

const OsSyscallCatalogDescriptor* syscall_descriptor_lookup(uint64_t number) {
    if (number == 0 || number > OS64_SYSCALL_CATALOG_COUNT) {
        return 0;
    }
    const OsSyscallCatalogDescriptor* descriptor =
        &os64_syscall_catalog[number - 1u];
    return descriptor->number == number ? descriptor : 0;
}

OsResult syscall_dispatch_preflight64(
    const OsSyscallCatalogDescriptor* descriptor,
    Process* process,
    Thread* thread,
    const uint64_t arguments[3],
    uint32_t* reject_reason_out) {
    set_reason(SYSCALL_REJECT_NONE, reject_reason_out);
    if (descriptor == 0 || arguments == 0) {
        set_reason(SYSCALL_REJECT_UNKNOWN_NUMBER, reject_reason_out);
        return SYS_ERR_UNSUPPORTED;
    }
    if (process == 0 || process->pid == 0) {
        set_reason(SYSCALL_REJECT_NO_PROCESS_CONTEXT, reject_reason_out);
        return SYS_ERR_NOT_READY;
    }
    if (!process->active || process->exiting ||
        process->state != PROCESS_STATE_RUNNING) {
        set_reason(SYSCALL_REJECT_PROCESS_LIFECYCLE, reject_reason_out);
        return SYS_ERR_NOT_READY;
    }
    if (thread == 0 || !thread->active || thread->exited) {
        set_reason(SYSCALL_REJECT_NO_THREAD_CONTEXT, reject_reason_out);
        return SYS_ERR_NOT_READY;
    }
    if (thread->owner != process || thread->owner_pid != process->pid ||
        thread->owner_generation != process->generation) {
        set_reason(SYSCALL_REJECT_THREAD_OWNER, reject_reason_out);
        return SYS_ERR_NOT_READY;
    }
    if ((process->permissions & ~OS_PROCESS_PERMISSION_VALID_MASK) != 0) {
        set_reason(SYSCALL_REJECT_PERMISSION_STATE, reject_reason_out);
        return SYS_ERR_PERMISSION_DENIED;
    }
    if ((descriptor->authority_mode == OS64_SYSCALL_AUTHORITY_PERMISSIONS ||
         descriptor->authority_mode ==
             OS64_SYSCALL_AUTHORITY_PERMISSIONS_THEN_SUBSYSTEM) &&
        !process_has_permissions(process, descriptor->required_permissions)) {
        set_reason(SYSCALL_REJECT_PERMISSION_DENIED, reject_reason_out);
        return SYS_ERR_PERMISSION_DENIED;
    }

    // A partial-output handler owns pointer ordering: validating every output
    // up front would incorrectly turn its documented partial publication into
    // an atomic operation.
    if (descriptor->output_publication == OS64_SYSCALL_OUTPUT_PARTIAL) {
        return OS_SUCCESS;
    }

    for (uint32_t index = 0; index < descriptor->argument_count; index++) {
        const uint8_t bit = (uint8_t)(1u << index);
        if ((descriptor->pointer_mask & bit) == 0) {
            continue;
        }
        const uint64_t address = arguments[index];
        const uint8_t size_register =
            descriptor->argument_size_register[index];
        const uint64_t range_size =
            size_register > 0 && size_register <= 3
                ? arguments[size_register - 1u]
                : 1u;
        if (size_register != 0 && range_size == 0) {
            continue;
        }
        if (address == 0 && (descriptor->nullable_mask & bit) != 0 &&
            size_register == 0) {
            continue;
        }
        if (address == 0) {
            set_reason(SYSCALL_REJECT_NULL_POINTER, reject_reason_out);
            return SYS_ERR_BAD_BUFFER;
        }
        const uint32_t alignment = descriptor->argument_alignment[index];
        if (size_register > 0 && size_register <= 3) {
            uint64_t last_address = 0;
            if (user_checked_add_u64(address,
                                     range_size - 1u,
                                     &last_address) !=
                    OS_SUCCESS) {
                set_reason(SYSCALL_REJECT_POINTER_FORMAT, reject_reason_out);
                return SYS_ERR_OVERFLOW;
            }
        }
        if (!user_address_is_canonical(address) || alignment == 0 ||
            (alignment & (alignment - 1u)) != 0 ||
            (address & (alignment - 1u)) != 0) {
            set_reason(SYSCALL_REJECT_POINTER_FORMAT, reject_reason_out);
            return SYS_ERR_BAD_BUFFER;
        }
        UserMemoryLease lease;
        const uint32_t access = (descriptor->writable_mask & bit) != 0
            ? USER_MEMORY_WRITE
            : USER_MEMORY_READ;
        const OsResult result = user_memory_lease_begin(process,
                                                        address,
                                                        1,
                                                        alignment,
                                                        access,
                                                        0,
                                                        &lease);
        if (result != OS_SUCCESS) {
            set_reason(result == SYS_ERR_NOT_READY
                           ? SYSCALL_REJECT_EXECUTION_CONTEXT
                           : SYSCALL_REJECT_POINTER_ACCESS,
                       reject_reason_out);
            return result;
        }
        user_memory_lease_end(&lease);
    }
    return OS_SUCCESS;
}

extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no,
                                       uint64_t arg1,
                                       uint64_t arg2,
                                       uint64_t arg3) {
    __atomic_add_fetch(&total_calls, 1u, __ATOMIC_RELAXED);
    const OsSyscallCatalogDescriptor* descriptor =
        syscall_descriptor_lookup(syscall_no);
    if (descriptor == 0) {
        record_rejection(syscall_no, SYSCALL_REJECT_UNKNOWN_NUMBER);
        return (uint64_t)(int64_t)SYS_ERR_UNSUPPORTED;
    }

    const uint64_t arguments[3] = {arg1, arg2, arg3};
    uint32_t reason = SYSCALL_REJECT_NONE;
    const OsResult preflight = syscall_dispatch_preflight64(descriptor,
                                                            current_process(),
                                                            current_thread(),
                                                            arguments,
                                                            &reason);
    if (preflight != OS_SUCCESS) {
        record_rejection(syscall_no, reason);
        return (uint64_t)(int64_t)preflight;
    }
    __atomic_add_fetch(&dispatched_calls, 1u, __ATOMIC_RELAXED);
    return syscall_dispatch_handler64(syscall_no, arg1, arg2, arg3);
}

uint32_t kernel_syscall_count() {
    return (uint32_t)__atomic_load_n(&total_calls, __ATOMIC_ACQUIRE);
}

void syscall_dispatch_get_diagnostics(SyscallDispatchDiagnostics* output) {
    if (output == 0) {
        return;
    }
    output->total_calls = __atomic_load_n(&total_calls, __ATOMIC_ACQUIRE);
    output->dispatched_calls =
        __atomic_load_n(&dispatched_calls, __ATOMIC_ACQUIRE);
    output->rejected_calls = __atomic_load_n(&rejected_calls, __ATOMIC_ACQUIRE);
    for (uint32_t reason = 0; reason < SYSCALL_REJECT_REASON_COUNT; reason++) {
        output->rejected_by_reason[reason] =
            __atomic_load_n(&rejected_by_reason[reason], __ATOMIC_ACQUIRE);
    }
    output->last_rejected_number =
        __atomic_load_n(&last_rejected_number, __ATOMIC_ACQUIRE);
    output->last_rejected_reason =
        __atomic_load_n(&last_rejected_reason, __ATOMIC_ACQUIRE);
    output->reserved0 = 0;
}

void syscall_dispatch_reset_diagnostics() {
    __atomic_store_n(&total_calls, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&dispatched_calls, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&rejected_calls, 0u, __ATOMIC_RELEASE);
    for (uint32_t reason = 0; reason < SYSCALL_REJECT_REASON_COUNT; reason++) {
        __atomic_store_n(&rejected_by_reason[reason], 0u, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&last_rejected_number, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&last_rejected_reason,
                     SYSCALL_REJECT_NONE,
                     __ATOMIC_RELEASE);
}

const char* syscall_reject_reason_name(uint32_t reason) {
    static const char* const names[SYSCALL_REJECT_REASON_COUNT] = {
        "none",
        "unknown_number",
        "no_process_context",
        "process_lifecycle",
        "no_thread_context",
        "thread_owner",
        "permission_state",
        "permission_denied",
        "null_pointer",
        "pointer_format",
        "pointer_access",
        "execution_context",
    };
    return reason < SYSCALL_REJECT_REASON_COUNT ? names[reason] : "invalid";
}
