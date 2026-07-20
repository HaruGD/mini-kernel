#include <stdint.h>

#include "drivers/gop.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "drivers/terminal.h"
#include "kernel/graphics/display_owner.h"
#include "kernel/input/input_events.h"
#include "kernel/ipc/ipc.h"
#include "kernel/process64.h"
#include "kernel/process_surface.h"
#include "kernel/service/service_registry.h"
#include "kernel/handle/kernel_objects.h"
#include "kernel/graphics/display_backend.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"
#include "kernel/syscall/sdk_syscalls.h"
#include "kernel/sync/thread_sync.h"
#include "os64/input_types.h"
#include "os64/process_types.h"
#include "os64/service_types.h"
#include "os64/surface_types.h"
#include "os64/thread_types.h"

extern Terminal terminal;

struct UserGraphicsRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
};

static_assert(sizeof(OsGraphicsInfo) == 16, "OsGraphicsInfo ABI changed");
static_assert(sizeof(UserGraphicsRect) == 20, "UserGraphicsRect ABI changed");
static_assert(sizeof(OsKeyEvent) == 16, "OsKeyEvent ABI changed");
static_assert(sizeof(OsInputEvent) == 48, "OsInputEvent ABI changed");
static_assert(sizeof(OsIpcMessage) == 88, "OsIpcMessage ABI changed");
static_assert(sizeof(OsIpcMessageV2) == 152, "OsIpcMessageV2 ABI changed");
static_assert(sizeof(OsIpcReceiveFilter) == 24, "OsIpcReceiveFilter ABI changed");
static_assert(sizeof(OsServiceInfo) == 36, "OsServiceInfo ABI changed");
static_assert(sizeof(OsProcessIdentity) == 8, "OsProcessIdentity ABI changed");
static_assert(sizeof(OsThreadIdentity) == 8, "OsThreadIdentity ABI changed");
static_assert(sizeof(OsThreadCreateRequest) == 40,
              "OsThreadCreateRequest ABI changed");

static uint64_t invalid_argument();
static uint64_t bad_buffer();

static uint64_t dispatch_thread_create(uint64_t user_request_address,
                                       uint64_t user_identity_address) {
    Process* process = current_process();
    if (process == 0 ||
        !user_buffer_writable((uint8_t*)(uintptr_t)user_identity_address,
                              sizeof(OsThreadIdentity))) {
        return bad_buffer();
    }
    OsThreadCreateRequest request;
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_request_address,
                          (uint8_t*)&request,
                          sizeof(request))) {
        return bad_buffer();
    }
    if (request.size != sizeof(request) || request.reserved != 0 ||
        request.flags != OS_THREAD_CREATE_FLAG_NONE) {
        return invalid_argument();
    }
    ThreadIdentity identity;
    int result = thread_create_user(process,
                                    request.entry,
                                    request.argument,
                                    request.return_trampoline,
                                    request.stack_size,
                                    request.flags,
                                    &identity);
    if (result != 0) {
        return (uint64_t)(int64_t)result;
    }
    OsThreadIdentity user_identity;
    user_identity.tid = identity.tid;
    user_identity.generation = identity.generation;
    if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_identity_address,
                                    (const uint8_t*)&user_identity,
                                    sizeof(user_identity))) {
        Thread* created = find_thread_by_identity(identity);
        if (created != 0) {
            thread_mark_exited(created, (uint32_t)SYS_ERR_BAD_BUFFER);
            thread_release_runtime(created);
        }
        return bad_buffer();
    }
    return 0;
}

static uint64_t dispatch_thread_self(uint64_t user_identity_address) {
    Thread* thread = current_thread();
    if (thread == 0 ||
        !user_buffer_writable((uint8_t*)(uintptr_t)user_identity_address,
                              sizeof(OsThreadIdentity))) {
        return bad_buffer();
    }
    OsThreadIdentity identity;
    identity.tid = thread->tid;
    identity.generation = thread->generation;
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_identity_address,
                                      (const uint8_t*)&identity,
                                      sizeof(identity))
        ? 0
        : bad_buffer();
}

static uint64_t dispatch_thread_join(uint64_t tid,
                                     uint64_t generation,
                                     uint64_t user_status_address) {
    if (tid > UINT32_MAX || generation > UINT32_MAX || tid == 0 || generation == 0) {
        return invalid_argument();
    }
    if (user_status_address != 0 &&
        !user_buffer_writable((uint8_t*)(uintptr_t)user_status_address,
                              sizeof(uint32_t))) {
        return bad_buffer();
    }
    ThreadIdentity identity;
    identity.tid = (uint32_t)tid;
    identity.generation = (uint32_t)generation;
    uint32_t immediate_status = 0;
    int result = thread_join_begin(current_thread(),
                                   identity,
                                   user_status_address,
                                   0,
                                   pit.get_tick(),
                                   &immediate_status);
    if (result < 0) {
        return (uint64_t)(int64_t)result;
    }
    if (result == 0) {
        return SYSCALL_WAIT_TO_KERNEL;
    }
    if (user_status_address != 0 &&
        !copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_status_address,
                                    (const uint8_t*)&immediate_status,
                                    sizeof(immediate_status))) {
        return bad_buffer();
    }
    return 0;
}

static uint64_t invalid_argument() {
    return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
}

static uint64_t bad_buffer() {
    return (uint64_t)(int64_t)SYS_ERR_BAD_BUFFER;
}

static int current_process_has(uint32_t permissions) {
    return process_has_permissions(current_process(), permissions);
}

static uint64_t permission_denied() {
    return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
}

static int text_equals(const char* left, const char* right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

static int current_process_is_display_authority() {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_DISPLAY)) {
        return 0;
    }
    if (text_equals(process->name, "usdk_test.elf") ||
        text_equals(process->name, "ugfxdemo_c.elf")) {
        return 1;
    }
    OsProcessIdentity owner;
    return service_find_owner_identity("display", &owner) == SERVICE_OK &&
           owner.pid == process->pid && owner.generation == process->generation;
}

static int process_is_input_authority(Process* process) {
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_INPUT)) {
        return 0;
    }
    OsProcessIdentity owner;
    return service_find_owner_identity("input", &owner) == SERVICE_OK &&
           owner.pid == process->pid && owner.generation == process->generation;
}

static int current_process_is_service_owner(const char* name,
                                            OsProcessIdentity* identity) {
    Process* process = current_process();
    OsProcessIdentity owner;
    if (process == 0 ||
        service_find_owner_identity(name, &owner) != SERVICE_OK ||
        owner.pid != process->pid || owner.generation != process->generation) {
        return 0;
    }
    if (identity != 0) {
        *identity = owner;
    }
    return 1;
}

static int restore_console_scanout() {
    input_events_discard_all();
    int result = terminal.redraw();
    display_session_finish_restore();
    return result;
}

static uint64_t dispatch_surface_create(uint64_t width,
                                        uint64_t height,
                                        uint64_t pixel_format) {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE)) {
        return permission_denied();
    }
    if (width > UINT32_MAX || height > UINT32_MAX || pixel_format > UINT32_MAX) {
        return invalid_argument();
    }
    uint64_t handle = kernel_graphics_surface_create(
        &process->handle_table,
        process->pid,
        (uint32_t)width,
        (uint32_t)height,
        (uint32_t)pixel_format,
        OS_SURFACE_APPLICATION_RIGHTS);
    return handle != 0 ? handle : (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
}

static uint64_t dispatch_surface_get_info(uint64_t handle, uint64_t user_info_address) {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE)) {
        return permission_denied();
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    KERNEL_HANDLE_RIGHT_READ,
                                    &resolved)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }
    KernelGraphicsSurfaceInfo info;
    if (kernel_graphics_surface_get_info(resolved.object, &info) != KERNEL_OBJECT_OK) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_info_address,
                                      (const uint8_t*)&info,
                                      sizeof(info))
        ? 0
        : bad_buffer();
}

static uint64_t dispatch_surface_map(uint64_t handle, uint64_t map_flags) {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE)) {
        return permission_denied();
    }
    if (map_flags > UINT32_MAX) {
        return invalid_argument();
    }
    return process_surface_map(process, handle, (uint32_t)map_flags);
}

static uint64_t dispatch_surface_unmap(uint64_t handle, uint64_t user_address) {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE)) {
        return permission_denied();
    }
    return (uint64_t)(int64_t)process_surface_unmap(process, handle, user_address);
}

static uint64_t dispatch_surface_close(uint64_t handle) {
    Process* process = current_process();
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE)) {
        return permission_denied();
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    0,
                                    &resolved)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }
    int unmap_result = process_surface_unmap_object(process, resolved.object);
    if (unmap_result != 0) {
        return (uint64_t)(int64_t)unmap_result;
    }
    return kernel_object_close_handle(&process->handle_table, handle, 0)
        ? 0
        : (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
}

static uint64_t dispatch_handle_close(uint64_t handle) {
    Process* process = current_process();
    if (process == 0) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_NONE,
                                    0,
                                    &resolved) ||
        (resolved.type != KERNEL_HANDLE_TYPE_SHARED_MEMORY &&
         resolved.type != KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE &&
         resolved.type != KERNEL_HANDLE_TYPE_MUTEX &&
         resolved.type != KERNEL_HANDLE_TYPE_SEMAPHORE &&
         resolved.type != KERNEL_HANDLE_TYPE_CONDITION)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }
    if (resolved.type == KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE) {
        int unmap_result = process_surface_unmap_object(process, resolved.object);
        if (unmap_result != 0) {
            return (uint64_t)(int64_t)unmap_result;
        }
    }
    return kernel_object_close_handle(&process->handle_table, handle, 0)
        ? 0
        : (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
}

static int pop_current_input_event(OsInputEvent* event) {
    Process* process = current_process();
    if (process_is_input_authority(process)) {
        return display_session_gui_active() ? input_events_pop(event) : 0;
    }
    if (process != 0) {
        return process_event_queue_pop(process, event);
    }
    return input_events_pop(event);
}

static uint64_t dispatch_ipc_send(uint64_t target_pid, uint64_t user_message_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    OsIpcMessage message;
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_message_address,
                          (uint8_t*)&message,
                          sizeof(message))) {
        return bad_buffer();
    }

    Process* sender = current_process();
    Process* target = find_process_by_pid((uint32_t)target_pid);
    return (uint64_t)(int64_t)ipc_send_message(sender, target, &message);
}

static uint64_t dispatch_ipc_send_identity(uint64_t target_pid,
                                           uint64_t target_generation,
                                           uint64_t user_message_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    OsIpcMessage message;
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_message_address,
                          (uint8_t*)&message,
                          sizeof(message))) {
        return bad_buffer();
    }

    Process* sender = current_process();
    Process* target = find_process_by_identity_compat((uint32_t)target_pid,
                                                      (uint32_t)target_generation);
    return (uint64_t)(int64_t)ipc_send_message(sender, target, &message);
}

static uint64_t dispatch_ipc_query(uint64_t user_version_address, uint64_t user_features_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    uint32_t version = OS64_IPC_ABI_VERSION_V2;
    uint32_t features = OS_IPC_FEATURE_V2 |
                        OS_IPC_FEATURE_CORRELATION |
                        OS_IPC_FEATURE_HANDLE_TRANSFER;
    if (user_version_address != 0 &&
        !copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_version_address,
                                    (const uint8_t*)&version,
                                    sizeof(version))) {
        return bad_buffer();
    }
    if (user_features_address != 0 &&
        !copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_features_address,
                                    (const uint8_t*)&features,
                                    sizeof(features))) {
        return bad_buffer();
    }
    return 0;
}

static uint64_t dispatch_ipc_v2_send_identity(uint64_t target_pid,
                                              uint64_t target_generation,
                                              uint64_t user_message_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    OsIpcMessageV2 message;
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_message_address,
                          (uint8_t*)&message,
                          sizeof(message))) {
        return bad_buffer();
    }

    Process* sender = current_process();
    Process* target = find_process_by_identity_compat((uint32_t)target_pid,
                                                      (uint32_t)target_generation);
    return (uint64_t)(int64_t)ipc_send_message_v2(sender, target, &message);
}

static uint64_t copy_ipc_message_to_user(uint64_t user_message_address,
                                         const OsIpcMessage* message) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessage))) {
        return bad_buffer();
    }
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_message_address,
                                      (const uint8_t*)message,
                                      sizeof(OsIpcMessage))
        ? 0
        : bad_buffer();
}

static uint64_t copy_ipc_message_v2_to_user(uint64_t user_message_address,
                                            const OsIpcMessageV2* message) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessageV2))) {
        return bad_buffer();
    }
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_message_address,
                                      (const uint8_t*)message,
                                      sizeof(OsIpcMessageV2))
        ? 0
        : bad_buffer();
}

static uint64_t dispatch_ipc_v2_receive(uint64_t user_message_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessageV2))) {
        return bad_buffer();
    }

    OsIpcMessageV2 message;
    int result = ipc_receive_message_v2(current_process(), &message);
    if (result != IPC_OK) {
        return (uint64_t)(int64_t)result;
    }
    return copy_ipc_message_v2_to_user(user_message_address, &message);
}

static uint64_t dispatch_ipc_v2_wait(uint64_t user_message_address,
                                     uint32_t timeout_ticks) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessageV2))) {
        return bad_buffer();
    }
    Process* receiver = current_process();
    OsIpcMessageV2 message;
    int result = ipc_receive_message_v2(receiver, &message);
    if (result == IPC_OK) {
        return copy_ipc_message_v2_to_user(user_message_address, &message);
    }
    if (result != IPC_ERR_WOULD_BLOCK) {
        return (uint64_t)(int64_t)result;
    }
    if (!process_wait_begin(receiver,
                            PROCESS_WAIT_IPC,
                            user_message_address,
                            timeout_ticks,
                            pit.get_tick())) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    Thread* thread = current_thread();
    if (thread != 0) {
        thread->context->wait_reserved[0] = 1;
    }
    if (process_ipc_mailbox_count(receiver) != 0) {
        process_wait_signal(receiver, PROCESS_WAIT_IPC, PROCESS_WAIT_OK);
    }
    return SYSCALL_WAIT_TO_KERNEL;
}

static uint64_t dispatch_ipc_v2_receive_match(uint64_t user_filter_address,
                                              uint64_t user_message_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    OsIpcReceiveFilter filter;
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_filter_address,
                          (uint8_t*)&filter,
                          sizeof(filter))) {
        return bad_buffer();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessageV2))) {
        return bad_buffer();
    }

    OsIpcMessageV2 message;
    int result = ipc_receive_message_v2_match(current_process(), &filter, &message);
    if (result != IPC_OK) {
        return (uint64_t)(int64_t)result;
    }
    return copy_ipc_message_v2_to_user(user_message_address, &message);
}

static uint64_t dispatch_ipc_receive(uint64_t user_message_address,
                                     bool wait,
                                     uint32_t timeout_ticks) {
    if (!current_process_has(OS_PROCESS_PERMISSION_IPC)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_message_address,
                              sizeof(OsIpcMessage))) {
        return bad_buffer();
    }

    Process* receiver = current_process();
    OsIpcMessage message;
    int result = ipc_receive_message(receiver, &message);
    if (result == IPC_OK) {
        return copy_ipc_message_to_user(user_message_address, &message);
    }
    if (!wait || result != IPC_ERR_WOULD_BLOCK) {
        return (uint64_t)(int64_t)result;
    }
    if (!process_wait_begin(receiver,
                            PROCESS_WAIT_IPC,
                            user_message_address,
                            timeout_ticks,
                            pit.get_tick())) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (process_ipc_mailbox_count(receiver) != 0) {
        process_wait_signal(receiver, PROCESS_WAIT_IPC, PROCESS_WAIT_OK);
    }
    return SYSCALL_WAIT_TO_KERNEL;
}

static uint64_t dispatch_service_register(uint64_t user_name_address, uint64_t flags) {
    if (!current_process_has(OS_PROCESS_PERMISSION_SERVICE_REGISTER)) {
        return permission_denied();
    }
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address, name, sizeof(name))) {
        return invalid_argument();
    }
    return (uint64_t)(int64_t)service_register(current_process(), name, (uint32_t)flags);
}

static uint64_t dispatch_service_find(uint64_t user_name_address, uint64_t user_info_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_SERVICE_DISCOVER)) {
        return permission_denied();
    }
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address, name, sizeof(name))) {
        return invalid_argument();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_info_address, sizeof(OsServiceInfo))) {
        return bad_buffer();
    }

    OsServiceInfo info;
    int result = service_find(name, &info);
    if (result != SERVICE_OK) {
        return (uint64_t)(int64_t)result;
    }
    if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_info_address,
                                    (const uint8_t*)&info,
                                    sizeof(info))) {
        return bad_buffer();
    }
    return 0;
}

static uint64_t dispatch_service_unregister(uint64_t user_name_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_SERVICE_REGISTER)) {
        return permission_denied();
    }
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address, name, sizeof(name))) {
        return invalid_argument();
    }
    Process* process = current_process();
    uint32_t pid = process != 0 ? process->pid : 0;
    uint32_t generation = process != 0 ? process->generation : 0;
    int result = service_unregister(process, name);
    if (result == SERVICE_OK &&
        display_session_begin_recovery(pid, generation)) {
        restore_console_scanout();
    }
    return (uint64_t)(int64_t)result;
}

static uint64_t dispatch_service_find_owner_identity(
    uint64_t user_name_address,
    uint64_t user_identity_address) {
    if (!current_process_has(OS_PROCESS_PERMISSION_SERVICE_DISCOVER)) {
        return permission_denied();
    }
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address,
                           name,
                           sizeof(name))) {
        return invalid_argument();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_identity_address,
                              sizeof(OsProcessIdentity))) {
        return bad_buffer();
    }
    OsProcessIdentity identity;
    int result = service_find_owner_identity(name, &identity);
    if (result != SERVICE_OK) {
        return (uint64_t)(int64_t)result;
    }
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_identity_address,
                                      (const uint8_t*)&identity,
                                      sizeof(identity))
        ? 0
        : bad_buffer();
}

static uint64_t dispatch_process_identity(uint64_t user_identity_address) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_identity_address,
                              sizeof(OsProcessIdentity))) {
        return bad_buffer();
    }

    Process* process = current_process();
    OsProcessIdentity identity;
    identity.pid = process != 0 ? process->pid : 0;
    identity.generation = process != 0 ? process->generation : 0;
    if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_identity_address,
                                    (const uint8_t*)&identity,
                                    sizeof(identity))) {
        return bad_buffer();
    }
    return 0;
}

static uint64_t dispatch_process_identity_alive(uint64_t pid,
                                                uint64_t generation) {
    if (!current_process_has(OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
        return permission_denied();
    }
    if (pid == 0 || generation == 0 || pid > UINT32_MAX ||
        generation > UINT32_MAX) {
        return invalid_argument();
    }
    ProcessIdentity identity;
    identity.pid = (uint32_t)pid;
    identity.generation = (uint32_t)generation;
    Process* process = find_process_by_identity(identity);
    if (process == 0 || process->state == PROCESS_STATE_RETURNED ||
        process->state == PROCESS_STATE_FAILED) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }
    return 0;
}

static uint64_t dispatch_graphics(uint64_t syscall_no,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3) {
    if (!current_process_is_display_authority()) {
        return permission_denied();
    }
    if (syscall_no == SYS_GFX_GET_INFO) {
        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }

        OsGraphicsInfo user_info;
        user_info.width = info->width;
        user_info.height = info->height;
        user_info.pixels_per_scanline = info->pixels_per_scanline;
        user_info.format = info->format;
        if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg1,
                                        (const uint8_t*)&user_info,
                                        sizeof(user_info))) {
            return invalid_argument();
        }
        return 0;
    }

    if (syscall_no == SYS_GFX_PUT_PIXEL) {
        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (arg1 >= info->width || arg2 >= info->height) {
            return (uint64_t)(int64_t)SYS_ERR_OUT_OF_RANGE;
        }
        gop.putpixel((uint32_t)arg1, (uint32_t)arg2, (uint32_t)arg3);
        return 0;
    }

    if (syscall_no == SYS_GFX_FILL_RECT) {
        UserGraphicsRect rect;
        if (!copy_user_buffer((const uint8_t*)(uintptr_t)arg1,
                              (uint8_t*)&rect,
                              sizeof(rect))) {
            return invalid_argument();
        }

        const GOPInfo* info = gop.info();
        if (info == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (rect.width == 0 || rect.height == 0) {
            return invalid_argument();
        }
        if (rect.x >= info->width || rect.y >= info->height) {
            return (uint64_t)(int64_t)SYS_ERR_OUT_OF_RANGE;
        }
        gop.fill_rect(rect.x, rect.y, rect.width, rect.height, rect.color);
        return 0;
    }

    if (!gop.ready()) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    gop.clear((uint32_t)arg1);
    return 0;
}

static uint64_t dispatch_graphics_present_surface(uint64_t handle,
                                                  uint64_t user_rects_address,
                                                  uint64_t rect_count_value) {
    if (!current_process_is_display_authority()) {
        return permission_denied();
    }
    if (rect_count_value == 0 ||
        rect_count_value > DISPLAY_BACKEND_MAX_DAMAGE_RECTS) {
        return invalid_argument();
    }
    Process* process = current_process();
    OsProcessIdentity display_identity;
    if (current_process_is_service_owner("display", &display_identity) &&
        display_session_gui_active() &&
        !display_session_present_allowed(display_identity.pid,
                                         display_identity.generation)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    KERNEL_HANDLE_RIGHT_READ | KERNEL_HANDLE_RIGHT_MAP,
                                    &resolved) ||
        (resolved.rights & (KERNEL_HANDLE_RIGHT_WRITE |
                            KERNEL_HANDLE_RIGHT_TRANSFER)) != 0) {
        return permission_denied();
    }
    uint32_t rect_count = (uint32_t)rect_count_value;
    OsRect rects[DISPLAY_BACKEND_MAX_DAMAGE_RECTS];
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)user_rects_address,
                          (uint8_t*)rects,
                          rect_count * sizeof(OsRect))) {
        return bad_buffer();
    }
    GraphicsSurface* surface = kernel_graphics_surface_get(resolved.object);
    uint32_t presented_rects = 0;
    int result = display_backend_present(surface,
                                         rects,
                                         rect_count,
                                         &presented_rects);
    if (result == DISPLAY_BACKEND_ERR_NOT_READY) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (result == DISPLAY_BACKEND_ERR_INVALID) {
        return invalid_argument();
    }
    if (result != DISPLAY_BACKEND_OK) {
        return (uint64_t)(int64_t)SYS_ERR_IO;
    }
    return presented_rects;
}

static uint64_t dispatch_display_session_acquire(uint64_t handle,
                                                 uint64_t user_info_address) {
    Process* process = current_process();
    OsProcessIdentity window_identity;
    OsProcessIdentity display_identity;
    if (!process_has_permissions(process, OS_PROCESS_PERMISSION_SHARED_SURFACE) ||
        !current_process_is_service_owner("window", &window_identity)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_info_address,
                              sizeof(OsDisplaySessionInfo))) {
        return bad_buffer();
    }
    if (service_find_owner_identity("display", &display_identity) != SERVICE_OK) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    const GOPInfo* gop_info = gop.info();
    KernelHandle resolved;
    KernelGraphicsSurfaceInfo surface_info;
    if (gop_info == 0 ||
        !kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    KERNEL_HANDLE_RIGHT_WRITE |
                                        KERNEL_HANDLE_RIGHT_MAP,
                                    &resolved) ||
        kernel_graphics_surface_get_info(resolved.object, &surface_info) !=
            KERNEL_OBJECT_OK ||
        surface_info.width != gop_info->width ||
        surface_info.height != gop_info->height ||
        surface_info.pixel_format != gop_info->format) {
        return invalid_argument();
    }

    uint32_t generation = 0;
    if (!display_session_begin(window_identity.pid,
                               window_identity.generation,
                               display_identity.pid,
                               display_identity.generation,
                               gop_info->width,
                               gop_info->height,
                               &generation)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    GraphicsSurface* snapshot = kernel_graphics_surface_get(resolved.object);
    if (!gop.copy_scanout(snapshot) ||
        !kernel_handle_restrict_rights(&process->handle_table,
                                       handle,
                                       OS_SURFACE_TRANSFER_RIGHTS) ||
        !display_session_commit(generation)) {
        display_session_abort_acquire(generation);
        restore_console_scanout();
        return (uint64_t)(int64_t)SYS_ERR_IO;
    }
    input_events_discard_all();

    OsDisplaySessionInfo info;
    display_session_get_info(&info);
    if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_info_address,
                                    (const uint8_t*)&info,
                                    sizeof(info))) {
        if (display_session_begin_release(window_identity.pid,
                                          window_identity.generation,
                                          generation)) {
            restore_console_scanout();
        }
        return bad_buffer();
    }
    return 0;
}

static uint64_t dispatch_display_session_release(uint64_t generation_value) {
    OsProcessIdentity window_identity;
    if (generation_value == 0 || generation_value > UINT32_MAX) {
        return invalid_argument();
    }
    if (!current_process_is_service_owner("window", &window_identity)) {
        return permission_denied();
    }
    if (!display_session_begin_release(window_identity.pid,
                                       window_identity.generation,
                                       (uint32_t)generation_value)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    return restore_console_scanout()
        ? 0
        : (uint64_t)(int64_t)SYS_ERR_IO;
}

static uint64_t dispatch_display_session_get_info(uint64_t user_info_address) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_info_address,
                              sizeof(OsDisplaySessionInfo))) {
        return bad_buffer();
    }
    OsDisplaySessionInfo info;
    display_session_get_info(&info);
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_info_address,
                                      (const uint8_t*)&info,
                                      sizeof(info))
        ? 0
        : bad_buffer();
}

static int pop_process_key_event(Process* process, OsKeyEvent* event) {
    if (process == 0 || event == 0) {
        return 0;
    }

    OsInputEvent input_event;
    while (process_event_queue_pop(process, &input_event)) {
        if (input_event.type == OS_INPUT_EVENT_KEY) {
            *event = input_event.data.key;
            return 1;
        }
    }
    return 0;
}

static int pop_process_character(Process* process, uint32_t* character) {
    if (process == 0 || character == 0) {
        return 0;
    }

    OsInputEvent event;
    while (process_event_queue_pop(process, &event)) {
        if (event.type == OS_INPUT_EVENT_KEY &&
            event.data.key.type == OS_KEY_EVENT_DOWN &&
            event.data.key.character != 0) {
            *character = event.data.key.character;
            return 1;
        }
    }
    return 0;
}

static uint64_t dispatch_keyboard(uint64_t user_event_address,
                                  bool wait,
                                  uint32_t timeout_ticks) {
    if (!current_process_has(OS_PROCESS_PERMISSION_INPUT)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_event_address,
                              sizeof(OsKeyEvent))) {
        return invalid_argument();
    }

    Process* process = current_process();
    OsKeyEvent event;
    if (pop_process_key_event(process, &event)) {
        return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_event_address,
                                          (const uint8_t*)&event,
                                          sizeof(event))
            ? 0
            : invalid_argument();
    }
    if (!wait) {
        return (uint64_t)(int64_t)SYS_ERR_WOULD_BLOCK;
    }
    if (process_is_input_authority(process) &&
        !display_session_gui_active()) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (process == 0 || process_focused_pid() != process->pid) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (!process_wait_begin(process,
                            PROCESS_WAIT_KEY,
                            user_event_address,
                            timeout_ticks,
                            pit.get_tick())) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (process_event_queue_has_key(process)) {
        process_wait_signal(process, PROCESS_WAIT_KEY, PROCESS_WAIT_OK);
    }
    return SYSCALL_WAIT_TO_KERNEL;
}

static uint64_t dispatch_input_event(uint64_t user_event_address,
                                     bool wait,
                                     uint32_t timeout_ticks) {
    if (!current_process_has(OS_PROCESS_PERMISSION_INPUT)) {
        return permission_denied();
    }
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_event_address,
                              sizeof(OsInputEvent))) {
        return invalid_argument();
    }

    Process* process = current_process();
    if (process_is_input_authority(process) &&
        !display_session_gui_active()) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    OsInputEvent event;
    if (pop_current_input_event(&event)) {
        return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)user_event_address,
                                          (const uint8_t*)&event,
                                          sizeof(event))
            ? 0
            : invalid_argument();
    }
    if (!wait) {
        return (uint64_t)(int64_t)SYS_ERR_WOULD_BLOCK;
    }
    if (process == 0 ||
        (!process_is_input_authority(process) && process_focused_pid() != process->pid)) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (!process_wait_begin(process,
                            PROCESS_WAIT_INPUT,
                            user_event_address,
                            timeout_ticks,
                            pit.get_tick())) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    uint32_t queued = process_is_input_authority(process)
        ? input_events_pending()
        : process_event_queue_count(process);
    if (queued != 0) {
        process_wait_signal(process, PROCESS_WAIT_INPUT, PROCESS_WAIT_OK);
    }
    return SYSCALL_WAIT_TO_KERNEL;
}

void complete_waiting_syscall64(Thread* thread) {
    if (thread == 0 || thread->owner == 0 || thread->context == 0 ||
        thread->context->wait_pending ||
        thread->context->wait_reason == PROCESS_WAIT_NONE) {
        return;
    }
    Process* process = thread->owner;
    ThreadContext* context = thread->context;

    int32_t result = context->wait_result;
    if (result == PROCESS_WAIT_OK && context->wait_reason == PROCESS_WAIT_IPC) {
        if (context->wait_reserved[0] != 0) {
            OsIpcMessageV2 message;
            result = ipc_receive_message_v2(process, &message);
            if (result == IPC_OK) {
                result = (int32_t)(int64_t)copy_ipc_message_v2_to_user(
                    context->wait_user_address,
                    &message);
            }
        } else {
            OsIpcMessage message;
            result = ipc_receive_message(process, &message);
            if (result == IPC_OK) {
                result = (int32_t)(int64_t)copy_ipc_message_to_user(
                    context->wait_user_address,
                    &message);
            }
        }
    } else if (result == PROCESS_WAIT_OK && context->wait_reason == PROCESS_WAIT_INPUT) {
        OsInputEvent event;
        int has_event = process_is_input_authority(process)
            ? (display_session_gui_active() ? input_events_pop(&event) : 0)
            : process_event_queue_pop(process, &event);
        if (!has_event) {
            result = SYS_ERR_NOT_READY;
        } else if (!copy_kernel_to_user_buffer(
                       (uint8_t*)(uintptr_t)context->wait_user_address,
                       (const uint8_t*)&event,
                       sizeof(event))) {
            result = SYS_ERR_BAD_BUFFER;
        }
    } else if (result == PROCESS_WAIT_OK && context->wait_reason == PROCESS_WAIT_KEY) {
        OsKeyEvent event;
        if (!pop_process_key_event(process, &event)) {
            result = SYS_ERR_NOT_READY;
        } else if (!copy_kernel_to_user_buffer(
                       (uint8_t*)(uintptr_t)context->wait_user_address,
                       (const uint8_t*)&event,
                       sizeof(event))) {
            result = SYS_ERR_BAD_BUFFER;
        }
    } else if (result == PROCESS_WAIT_OK && context->wait_reason == PROCESS_WAIT_CHAR) {
        uint32_t character = 0;
        if (!pop_process_character(process, &character)) {
            result = SYS_ERR_NOT_READY;
        } else {
            result = (int32_t)character;
        }
    } else if (result == PROCESS_WAIT_OK &&
               context->wait_reason == PROCESS_WAIT_THREAD_JOIN) {
        uint32_t status = 0;
        result = thread_join_consume(thread, &status);
        if (result == 0 && context->wait_user_address != 0 &&
            !copy_kernel_to_user_buffer(
                (uint8_t*)(uintptr_t)context->wait_user_address,
                (const uint8_t*)&status,
                sizeof(status))) {
            result = SYS_ERR_BAD_BUFFER;
        }
    }

    context->saved_rax = (uint64_t)(int64_t)result;
    process_wait_reset(process);
}

bool dispatch_sdk_syscall64(uint64_t syscall_no,
                            uint64_t arg1,
                            uint64_t arg2,
                            uint64_t arg3,
                            uint64_t* result) {
    if (result == 0) {
        return false;
    }
    if (syscall_no == SYS_THREAD_CREATE) {
        *result = dispatch_thread_create(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_THREAD_SELF) {
        *result = dispatch_thread_self(arg1);
        return true;
    }
    if (syscall_no == SYS_THREAD_EXIT) {
        Thread* thread = current_thread();
        if (thread == 0) {
            *result = (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        } else {
            thread_mark_exited(thread, (uint32_t)arg1);
            *result = SYSCALL_RETURN_TO_KERNEL;
        }
        return true;
    }
    if (syscall_no == SYS_THREAD_JOIN) {
        *result = dispatch_thread_join(arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_MUTEX_CREATE) {
        uint64_t handle = kernel_mutex_create(current_process());
        *result = handle != 0 ? handle : (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
        return true;
    }
    if (syscall_no == SYS_MUTEX_LOCK) {
        *result = (uint64_t)kernel_mutex_lock(current_process(), current_thread(),
                                             arg1, (uint32_t)arg2, pit.get_tick());
        return true;
    }
    if (syscall_no == SYS_MUTEX_UNLOCK) {
        *result = (uint64_t)(int64_t)kernel_mutex_unlock(current_process(),
                                                           current_thread(), arg1);
        return true;
    }
    if (syscall_no == SYS_SEMAPHORE_CREATE) {
        uint64_t handle = kernel_semaphore_create(current_process(),
                                                  (uint32_t)arg1, (uint32_t)arg2);
        *result = handle != 0 ? handle : (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
        return true;
    }
    if (syscall_no == SYS_SEMAPHORE_WAIT) {
        *result = (uint64_t)kernel_semaphore_wait(current_process(), current_thread(),
                                                 arg1, (uint32_t)arg2, pit.get_tick());
        return true;
    }
    if (syscall_no == SYS_SEMAPHORE_POST) {
        *result = (uint64_t)(int64_t)kernel_semaphore_post(current_process(),
                                                              arg1, (uint32_t)arg2);
        return true;
    }
    if (syscall_no == SYS_CONDITION_CREATE) {
        uint64_t handle = kernel_condition_create(current_process());
        *result = handle != 0 ? handle : (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
        return true;
    }
    if (syscall_no == SYS_CONDITION_WAIT) {
        *result = (uint64_t)kernel_condition_wait(current_process(), current_thread(),
                                                 arg1, arg2, (uint32_t)arg3,
                                                 pit.get_tick());
        return true;
    }
    if (syscall_no == SYS_CONDITION_SIGNAL ||
        syscall_no == SYS_CONDITION_BROADCAST) {
        *result = (uint64_t)(int64_t)kernel_condition_signal(
            current_process(), arg1, syscall_no == SYS_CONDITION_BROADCAST);
        return true;
    }
    if (syscall_no == SYS_TIME_TICKS) {
        *result = pit.get_tick64();
        return true;
    }
    if (syscall_no == SYS_TIME_FREQUENCY) {
        *result = pit.get_frequency();
        return true;
    }
    if (syscall_no >= SYS_GFX_GET_INFO && syscall_no <= SYS_GFX_CLEAR) {
        *result = dispatch_graphics(syscall_no, arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_GFX_PRESENT_SURFACE) {
        *result = dispatch_graphics_present_surface(arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_DISPLAY_SESSION_ACQUIRE) {
        *result = dispatch_display_session_acquire(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_DISPLAY_SESSION_RELEASE) {
        *result = dispatch_display_session_release(arg1);
        return true;
    }
    if (syscall_no == SYS_DISPLAY_SESSION_GET_INFO) {
        *result = dispatch_display_session_get_info(arg1);
        return true;
    }
    if (syscall_no == SYS_KEYBOARD_EVENT) {
        *result = dispatch_keyboard(arg1, arg2 != 0, (uint32_t)arg3);
        return true;
    }
    if (syscall_no == SYS_INPUT_EVENT_POLL) {
        *result = dispatch_input_event(arg1, false, 0);
        return true;
    }
    if (syscall_no == SYS_INPUT_EVENT_WAIT) {
        *result = dispatch_input_event(arg1, true, (uint32_t)arg2);
        return true;
    }
    if (syscall_no == SYS_IPC_SEND) {
        *result = dispatch_ipc_send(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_IPC_SEND_IDENTITY) {
        *result = dispatch_ipc_send_identity(arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_IPC_QUERY) {
        *result = dispatch_ipc_query(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_IPC_V2_SEND_IDENTITY) {
        *result = dispatch_ipc_v2_send_identity(arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_IPC_V2_RECV) {
        *result = dispatch_ipc_v2_receive(arg1);
        return true;
    }
    if (syscall_no == SYS_IPC_V2_RECV_MATCH) {
        *result = dispatch_ipc_v2_receive_match(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_IPC_V2_WAIT) {
        *result = dispatch_ipc_v2_wait(arg1, (uint32_t)arg2);
        return true;
    }
    if (syscall_no == SYS_IPC_RECV) {
        *result = dispatch_ipc_receive(arg1, false, 0);
        return true;
    }
    if (syscall_no == SYS_IPC_WAIT) {
        *result = dispatch_ipc_receive(arg1, true, (uint32_t)arg2);
        return true;
    }
    if (syscall_no == SYS_SERVICE_REGISTER) {
        *result = dispatch_service_register(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SERVICE_FIND) {
        *result = dispatch_service_find(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SERVICE_UNREGISTER) {
        *result = dispatch_service_unregister(arg1);
        return true;
    }
    if (syscall_no == SYS_SERVICE_FIND_OWNER_IDENTITY) {
        *result = dispatch_service_find_owner_identity(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_GET_PROCESS_IDENTITY) {
        *result = dispatch_process_identity(arg1);
        return true;
    }
    if (syscall_no == SYS_PROCESS_IDENTITY_ALIVE) {
        *result = dispatch_process_identity_alive(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SURFACE_CREATE) {
        *result = dispatch_surface_create(arg1, arg2, arg3);
        return true;
    }
    if (syscall_no == SYS_SURFACE_GET_INFO) {
        *result = dispatch_surface_get_info(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SURFACE_MAP) {
        *result = dispatch_surface_map(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SURFACE_UNMAP) {
        *result = dispatch_surface_unmap(arg1, arg2);
        return true;
    }
    if (syscall_no == SYS_SURFACE_CLOSE) {
        *result = dispatch_surface_close(arg1);
        return true;
    }
    if (syscall_no == SYS_HANDLE_CLOSE) {
        *result = dispatch_handle_close(arg1);
        return true;
    }
    return false;
}
