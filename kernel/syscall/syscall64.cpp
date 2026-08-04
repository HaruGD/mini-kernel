#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "kernel/mm/heap.h"
}

#include "fs/vfs.h"
#include "drivers/terminal.h"
#include "kernel/boot_info.h"
#include "kernel/input/input_events.h"
#include "kernel/kernel_diag.h"
#include "kernel/kutil64.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/process_terminal.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"
#include "kernel/syscall/sdk_syscalls.h"
#include "kernel/syscall/vfs_syscalls.h"

#define USER_PATH_MAX PROCESS_CMDLINE_MAX
#define USER_WRITE_MAX 4096u
#define USER_WRITE_CHUNK_MAX 512u

extern Terminal terminal;

static uint32_t syscall_count = 0;

uint32_t kernel_syscall_count() {
    return syscall_count;
}

static int pop_keyboard_character_from_events(char* out_char) {
    if (out_char == 0) {
        return 0;
    }

    Process* process = current_process();
    while (1) {
        OsInputEvent event;
        int has_event = process != 0 ? process_event_queue_pop(process, &event) : input_events_pop(&event);
        if (!has_event) {
            return 0;
        }
        if (event.type != OS_INPUT_EVENT_KEY ||
            event.data.key.type != KEYBOARD_EVENT_DOWN ||
            event.data.key.character == 0) {
            continue;
        }

        *out_char = (char)event.data.key.character;
        return 1;
    }
}

extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code) {
    Thread* thread = current_thread();
    Process* process = current_process();
    if (process != 0 && thread != 0) {
        process->fault_thread_identity = thread_identity(thread);
        print("\nFaulting thread: tid=");
        print_hex32(thread->tid);
        print(" generation=");
        print_hex32(thread->generation);
    }
    process_mark_failed(process, reason, status_code);
}

extern "C" uint64_t process_fault_returnable64() {
    return current_process() != 0 ? 1 : 0;
}

#define print process_terminal_print
#define print_hex32 process_terminal_print_hex32
#define print_hex64 process_terminal_print_hex64

extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    syscall_count++;

    if (syscall_no == SYS_WRITE) {
        const uint8_t* user_buffer = (const uint8_t*)(uintptr_t)arg1;
        uint32_t length = (uint32_t)arg2;
        uint8_t chunk[USER_WRITE_CHUNK_MAX];
        uint32_t written = 0;

        if (length == 0) {
            return 0;
        }
        if (arg2 > USER_WRITE_MAX || user_buffer == 0) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }

        while (written < length) {
            uint32_t chunk_size = length - written;
            if (chunk_size > USER_WRITE_CHUNK_MAX) {
                chunk_size = USER_WRITE_CHUNK_MAX;
            }
            if (!copy_user_buffer(user_buffer + written, chunk, chunk_size)) {
                return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
            }
            Process* process = current_process();
            if (process_terminal_attached(process)) {
                int result = process_terminal_write(process, chunk, chunk_size);
                if (result < 0) {
                    return (uint64_t)(int64_t)result;
                }
            } else {
                print_n((const char*)chunk, chunk_size);
            }
            written += chunk_size;
        }
        return written;
    }

    if (syscall_no == SYS_EXIT) {
        process_mark_returned(current_process(), PROCESS_TERM_EXIT, (uint32_t)arg1);
        print("\nUser mode exit requested.");
        return SYSCALL_RETURN_TO_KERNEL;
    }

    if (syscall_no == SYS_PUTCHAR) {
        uint8_t character = (uint8_t)(arg1 & 0xFF);
        Process* process = current_process();
        if (process_terminal_attached(process)) {
            int result = process_terminal_write(process, &character, 1);
            return result < 0 ? (uint64_t)(int64_t)result : 1;
        }
        putchar_both((char)character);
        return 1;
    }

    if (syscall_no == SYS_CLEAR_SCREEN) {
        if (process_terminal_attached(current_process())) {
            return (uint64_t)(int64_t)process_terminal_clear(current_process());
        }
        terminal.clear();
        return 0;
    }

    if (syscall_no == SYS_TERMINAL_SESSION_BIND) {
        ProcessIdentity peer = {(uint32_t)arg1, (uint32_t)arg2};
        return (uint64_t)(int64_t)process_terminal_bind(current_process(), peer);
    }

    if (syscall_no == SYS_TERMINAL_SESSION_EXIT) {
        return (uint64_t)(int64_t)process_terminal_send_exit(
            current_process(), (int32_t)arg1);
    }

    if (syscall_no == SYS_TERMINAL_SESSION_READ) {
        if (!user_buffer_writable((uint8_t*)(uintptr_t)arg3,
                                  sizeof(OsTerminalPacket))) {
            return (uint64_t)(int64_t)SYS_ERR_BAD_BUFFER;
        }
        ProcessIdentity owner_identity = {(uint32_t)arg1, (uint32_t)arg2};
        OsTerminalPacket packet;
        int result = process_terminal_read_output(current_process(),
                                                  owner_identity, &packet);
        if (result < 0) return (uint64_t)(int64_t)result;
        return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg3,
                                          (const uint8_t*)&packet,
                                          sizeof(packet))
            ? 0 : (uint64_t)(int64_t)SYS_ERR_BAD_BUFFER;
    }

    if (syscall_no == SYS_TERMINAL_SESSION_CLOSE) {
        ProcessIdentity owner_identity = {(uint32_t)arg1, (uint32_t)arg2};
        return (uint64_t)(int64_t)process_terminal_close(
            current_process(), owner_identity);
    }

    if (syscall_no == SYS_USER_BRK) {
        Process* process = current_process();
        uint64_t result = resize_user_process_heap(process, arg1);
        return result != 0 ? result : (uint64_t)-1;
    }

    uint64_t sdk_result = 0;
    if (dispatch_sdk_syscall64(syscall_no, arg1, arg2, arg3, &sdk_result)) {
        return sdk_result;
    }

    if (syscall_no == SYS_GETCHAR) {
        Process* process = current_process();
        if (process_terminal_attached(process)) {
            uint8_t character = 0;
            int result = process_terminal_read_char(process, &character);
            if (result > 0) {
                return character;
            }
            if (result != SYS_ERR_WOULD_BLOCK) {
                return (uint64_t)(int64_t)result;
            }
            if (!process_wait_begin(process, PROCESS_WAIT_CHAR, 0, 0,
                                    pit.get_tick())) {
                return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
            }
            result = process_terminal_read_char(process, &character);
            if (result > 0 || result == SYS_ERR_CANCELLED) {
                process_wait_signal(process, PROCESS_WAIT_CHAR,
                                    result > 0 ? PROCESS_WAIT_OK
                                               : PROCESS_WAIT_CANCELLED);
            }
            return SYSCALL_WAIT_TO_KERNEL;
        }
        if (!process_has_permissions(current_process(), OS_PROCESS_PERMISSION_INPUT)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        char ascii = 0;
        if (pop_keyboard_character_from_events(&ascii)) {
            return (uint64_t)(unsigned char)ascii;
        }
        if (process == 0 || process_focused_pid() != process->pid) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (!process_wait_begin(process, PROCESS_WAIT_CHAR, 0, 0, pit.get_tick())) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (process_event_queue_has_character(process)) {
            process_wait_signal(process, PROCESS_WAIT_CHAR, PROCESS_WAIT_OK);
        }
        return SYSCALL_WAIT_TO_KERNEL;
    }

    if (syscall_no == SYS_LIST_FILES) {
        vfs_list_files();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_LIST_FILES_AT) {
        char file_path[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_path, sizeof(file_path))) {
            print("\nInvalid user path pointer.");
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }

        if (vfs_list_files_at(file_path) != VFS_OK) {
            print("\nFailed to list path: ");
            print(file_path);
            print("\n");
            return (uint64_t)-1;
        }
        print("\n");
        return 0;
    }

    if (dispatch_vfs_handle_syscall64(syscall_no, arg1, arg2, arg3, &sdk_result)) {
        return sdk_result;
    }

    if (syscall_no == SYS_MKDIR || syscall_no == SYS_RMDIR ||
        syscall_no == SYS_MKDIR_SILENT || syscall_no == SYS_RMDIR_SILENT) {
        char dir_path[USER_PATH_MAX];
        int silent = syscall_no == SYS_MKDIR_SILENT || syscall_no == SYS_RMDIR_SILENT;
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, dir_path, sizeof(dir_path))) {
            if (!silent) {
                print("\nInvalid user path pointer.");
            }
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }

        int result = (syscall_no == SYS_MKDIR || syscall_no == SYS_MKDIR_SILENT) ? vfs_mkdir(dir_path) : vfs_rmdir(dir_path);
        if (result == VFS_OK) {
            if (!silent) {
                print((syscall_no == SYS_MKDIR || syscall_no == SYS_MKDIR_SILENT) ? "\nCreated dir: " : "\nRemoved dir: ");
                print(dir_path);
                print("\n");
            }
            return 0;
        }

        if (!silent) {
            print((syscall_no == SYS_MKDIR || syscall_no == SYS_MKDIR_SILENT) ? "\nFailed to create dir: " : "\nFailed to remove dir: ");
            print(dir_path);
            print("\n");
        }
        return (uint64_t)(int64_t)result;
    }

    if (syscall_no == SYS_RENAME_PATH) {
        char old_path[USER_PATH_MAX];
        char new_path[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, old_path, sizeof(old_path)) ||
            !copy_user_cstring((const char*)(uintptr_t)arg2, new_path, sizeof(new_path))) {
            print("\nInvalid user path pointer.");
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }

        int result = vfs_rename(old_path, new_path);
        if (result != VFS_OK) {
            print("\nFailed to rename: ");
            print(old_path);
            print(" -> ");
            print(new_path);
            print("\n");
            return (uint64_t)(int64_t)result;
        }

        print("\nRenamed: ");
        print(old_path);
        print(" -> ");
        print(new_path);
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_VFS_INFO) {
        char file_path[USER_PATH_MAX];
        VFSFileInfo info;
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_path, sizeof(file_path))) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        if (arg2 == 0) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        int result = vfs_get_file_info(file_path, &info);
        if (result != VFS_OK) {
            return (uint64_t)(int64_t)result;
        }
        if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg2, (const uint8_t*)&info, sizeof(info))) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        return 0;
    }

    if (syscall_no == SYS_GETCWD) {
        Process* process = current_process();
        const char* cwd = process_get_cwd(process);
        uint32_t capacity = (uint32_t)arg2;
        uint32_t needed = (uint32_t)strlen64(cwd) + 1;
        if (arg1 == 0 || capacity < needed) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg1,
                                          (const uint8_t*)cwd,
                                          needed)
            ? 0
            : (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    if (syscall_no == SYS_CHDIR) {
        Process* process = current_process();
        char dir_path[USER_PATH_MAX];
        VFSFileInfo info;
        if (process == 0) {
            return (uint64_t)(int64_t)SYS_ERR_NOT_READY;
        }
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, dir_path, sizeof(dir_path))) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        int result = vfs_get_file_info(dir_path, &info);
        if (result != VFS_OK) {
            return (uint64_t)(int64_t)result;
        }
        if (info.type != VFS_NODE_DIR) {
            return (uint64_t)(int64_t)VFS_ERR_INVALID_PATH;
        }
        process_copy_cwd(process, dir_path);
        return 0;
    }

    if (syscall_no == SYS_CAT_FILE) {
        char file_name[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        VFSFileInfo file_info;
        if (vfs_get_file_info(file_name, &file_info) != VFS_OK) {
            print("\nFile not found: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }
        if (file_info.type != VFS_NODE_FILE) {
            print("\nNot a file: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        uint32_t buffer_size = file_info.size + 1;
        if (buffer_size < 512) {
            buffer_size = 512;
        }

        uint8_t* file_buffer = (uint8_t*)kmalloc(buffer_size);
        if (file_buffer == 0) {
            print("\nOut of memory reading file.\n");
            return (uint64_t)-1;
        }

        uint32_t bytes_read = 0;
        if (vfs_read_file(file_name, file_buffer, buffer_size, &bytes_read) != VFS_OK) {
            kfree(file_buffer);
            print("\nFailed to read file: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        file_buffer[bytes_read] = '\0';
        print((const char*)file_buffer);
        if (bytes_read == 0 || file_buffer[bytes_read - 1] != '\n') {
            print("\n");
        }
        kfree(file_buffer);
        return bytes_read;
    }

    if (syscall_no == SYS_RUN_USER) {
        if (!process_has_permissions(current_process(), OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        char command_line[PROCESS_CMDLINE_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, command_line, sizeof(command_line))) {
            print("\nInvalid user program pointer.");
            return (uint64_t)-1;
        }

        if (!run_user_program(command_line)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_RUN_USER_WITH_PERMISSIONS) {
        Process* parent = current_process();
        uint32_t permissions = (uint32_t)arg2;
        if (!process_has_permissions(parent, OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        if ((permissions & ~OS_PROCESS_PERMISSION_VALID_MASK) != 0) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        char command_line[PROCESS_CMDLINE_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, command_line, sizeof(command_line))) {
            return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
        }
        return run_user_program_with_permissions(command_line, permissions)
            ? 0
            : (uint64_t)(int64_t)SYS_ERR_NOT_READY;
    }

    if (syscall_no == SYS_VERSION) {
        command_version();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_BOOTINFO) {
        print_boot_info();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_MEMSTAT) {
        command_memstat();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_RM_FILE || syscall_no == SYS_RM_FILE_SILENT) {
        int noisy = syscall_no == SYS_RM_FILE;
        char file_name[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_delete_file(file_name) == VFS_OK) {
            if (noisy) {
                print("\nDeleted: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFile not found: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_UPTIME) {
        command_uptime();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_TOUCH_FILE || syscall_no == SYS_TOUCH_FILE_SILENT) {
        int noisy = syscall_no == SYS_TOUCH_FILE;
        char file_name[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_touch_file(file_name) == VFS_OK) {
            if (noisy) {
                print("\nTouched: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFailed to touch file: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_SAVE_FILE || syscall_no == SYS_SAVE_FILE_SILENT) {
        int noisy = syscall_no == SYS_SAVE_FILE;
        char file_name[USER_PATH_MAX];
        char file_text[128];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }
        if (!copy_user_cstring((const char*)(uintptr_t)arg2, file_text, sizeof(file_text))) {
            if (noisy) {
                print("\nInvalid user text pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_write_file(file_name, (uint8_t*)file_text, (uint32_t)strlen64(file_text)) == VFS_OK) {
            if (noisy) {
                print("\nSaved: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFailed to save file: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_GET_PID) {
        Process* process = current_process();
        return process != 0 ? process->pid : 0;
    }

    if (syscall_no == SYS_GET_PPID) {
        Process* process = current_process();
        return process != 0 ? process->parent_pid : 0;
    }

    if (syscall_no == SYS_PS) {
        print_process_table(pit.get_tick());
        return 0;
    }

    if (syscall_no == SYS_LAST_STATUS) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        const Process* child = find_last_child_process(process->pid);
        if (child == 0) {
            print("\nNo child program result.\n");
            return 0;
        }

        print_child_result_compact("Last child", child);
        print("\n");
        return child->status_code;
    }

    if (syscall_no == SYS_WAIT_CHILD) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        Process* child = find_waitable_child_process(process->pid);
        if (child == 0) {
            print("\nNo unreaped child result.\n");
            return 0;
        }

        print_child_result_compact("Wait child", child);
        print("\n");
        child->reaped = 1;
        return child->status_code;
    }

    if (syscall_no == SYS_SCHED_INFO) {
        print_scheduler_info(sched_queue,
                             sched_queue_count,
                             sched_queue_head,
                             SCHED_QUEUE_SIZE,
                             sched_last_pid,
                             sched_switch_count,
                             sched_yield_count,
                             process_focused_pid(),
                             pit.get_tick());
        return 0;
    }

    if (syscall_no == SYS_VFS_MOUNTS) {
        print_vfs_mounts();
        return 0;
    }

    if (syscall_no == SYS_YIELD) {
        return SYSCALL_YIELD_TO_KERNEL;
    }

    if (syscall_no == SYS_RESUME_USER) {
        if (!process_has_permissions(current_process(), OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        uint32_t pid = (uint32_t)arg1;
        if (!resume_user_program(pid)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_KILL_USER) {
        if (!process_has_permissions(current_process(), OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        uint32_t pid = (uint32_t)arg1;
        if (!kill_user_program(pid)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_REAP_ALL_CHILDREN) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        uint32_t count = reap_all_child_processes(process->pid);
        print("\nReaped children: ");
        print_hex32(count);
        print("\n");
        return count;
    }

    if (syscall_no == SYS_JOBS) {
        Process* process = current_process();
        print_jobs_for_process(process, pit.get_tick());
        return 0;
    }

    if (syscall_no == SYS_SLEEP) {
        uint32_t ticks = (uint32_t)arg1;
        if (ticks == 0) {
            ticks = 1;
        }
        return SYSCALL_SLEEP_TO_KERNEL;
    }

    if (syscall_no == SYS_SET_BACKGROUND) {
        if (!process_has_permissions(current_process(), OS_PROCESS_PERMISSION_MANAGE_CHILD)) {
            return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
        }
        uint32_t pid = (uint32_t)arg1;
        uint32_t enabled = (uint32_t)arg2;
        if (!set_user_program_background(pid, enabled)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_CHILDREN_ACTIVE) {
        Process* process = current_process();
        if (process == 0) {
            return 0;
        }
        return count_unfinished_child_processes(process->pid);
    }

    if (syscall_no == SYS_REAP_ALL_CHILDREN_SILENT) {
        Process* process = current_process();
        if (process == 0) {
            return 0;
        }
        return reap_all_child_processes(process->pid);
    }

    print("\nUnknown syscall: ");
    print_hex32((uint32_t)syscall_no);
    print("\n");
    return (uint64_t)-1;
}
