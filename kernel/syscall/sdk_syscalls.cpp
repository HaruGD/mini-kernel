#include <stdint.h>

#include "drivers/gop.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "kernel/input/input_events.h"
#include "kernel/ipc/ipc.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"
#include "kernel/syscall/sdk_syscalls.h"
#include "os64/input_types.h"
#include "os64/process_types.h"
#include "os64/service_types.h"

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

static uint64_t invalid_argument() {
    return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
}

static uint64_t bad_buffer() {
    return (uint64_t)(int64_t)SYS_ERR_BAD_BUFFER;
}

static int pop_current_input_event(OsInputEvent* event) {
    Process* process = current_process();
    if (process != 0) {
        return process_event_queue_pop(process, event);
    }
    return input_events_pop(event);
}

static uint64_t dispatch_ipc_send(uint64_t target_pid, uint64_t user_message_address) {
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

static uint64_t dispatch_ipc_v2_receive_match(uint64_t user_filter_address,
                                              uint64_t user_message_address) {
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
    return SYSCALL_WAIT_TO_KERNEL;
}

static uint64_t dispatch_service_register(uint64_t user_name_address, uint64_t flags) {
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address, name, sizeof(name))) {
        return invalid_argument();
    }
    return (uint64_t)(int64_t)service_register(current_process(), name, (uint32_t)flags);
}

static uint64_t dispatch_service_find(uint64_t user_name_address, uint64_t user_info_address) {
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
    char name[OS_SERVICE_NAME_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)user_name_address, name, sizeof(name))) {
        return invalid_argument();
    }
    return (uint64_t)(int64_t)service_unregister(current_process(), name);
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

static uint64_t dispatch_graphics(uint64_t syscall_no,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3) {
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
    return SYSCALL_WAIT_TO_KERNEL;
}

static uint64_t dispatch_input_event(uint64_t user_event_address,
                                     bool wait,
                                     uint32_t timeout_ticks) {
    if (!user_buffer_writable((uint8_t*)(uintptr_t)user_event_address,
                              sizeof(OsInputEvent))) {
        return invalid_argument();
    }

    Process* process = current_process();
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
    if (process == 0 || process_focused_pid() != process->pid) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    if (!process_wait_begin(process,
                            PROCESS_WAIT_INPUT,
                            user_event_address,
                            timeout_ticks,
                            pit.get_tick())) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }
    return SYSCALL_WAIT_TO_KERNEL;
}

void complete_waiting_syscall64(Process* process) {
    if (process == 0 || process->wait_pending || process->wait_reason == PROCESS_WAIT_NONE) {
        return;
    }

    int32_t result = process->wait_result;
    if (result == PROCESS_WAIT_OK && process->wait_reason == PROCESS_WAIT_IPC) {
        OsIpcMessage message;
        result = ipc_receive_message(process, &message);
        if (result == IPC_OK) {
            result = (int32_t)(int64_t)copy_ipc_message_to_user(process->wait_user_address, &message);
        }
    } else if (result == PROCESS_WAIT_OK && process->wait_reason == PROCESS_WAIT_INPUT) {
        OsInputEvent event;
        if (!process_event_queue_pop(process, &event)) {
            result = SYS_ERR_NOT_READY;
        } else if (!copy_kernel_to_user_buffer(
                       (uint8_t*)(uintptr_t)process->wait_user_address,
                       (const uint8_t*)&event,
                       sizeof(event))) {
            result = SYS_ERR_BAD_BUFFER;
        }
    } else if (result == PROCESS_WAIT_OK && process->wait_reason == PROCESS_WAIT_KEY) {
        OsKeyEvent event;
        if (!pop_process_key_event(process, &event)) {
            result = SYS_ERR_NOT_READY;
        } else if (!copy_kernel_to_user_buffer(
                       (uint8_t*)(uintptr_t)process->wait_user_address,
                       (const uint8_t*)&event,
                       sizeof(event))) {
            result = SYS_ERR_BAD_BUFFER;
        }
    } else if (result == PROCESS_WAIT_OK && process->wait_reason == PROCESS_WAIT_CHAR) {
        uint32_t character = 0;
        if (!pop_process_character(process, &character)) {
            result = SYS_ERR_NOT_READY;
        } else {
            result = (int32_t)character;
        }
    }

    process->saved_rax = (uint64_t)(int64_t)result;
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
    if (syscall_no == SYS_GET_PROCESS_IDENTITY) {
        *result = dispatch_process_identity(arg1);
        return true;
    }
    return false;
}
