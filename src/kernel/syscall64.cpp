#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "heap.h"
}

#include "fs/vfs.h"
#include "kernel/boot_info.h"
#include "kernel/kernel_diag.h"
#include "kernel/kutil64.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"

#define USER_PATH_MAX PROCESS_CMDLINE_MAX

static uint32_t syscall_count = 0;

uint32_t kernel_syscall_count() {
    return syscall_count;
}

extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code) {
    process_mark_failed(current_process(), reason, status_code);
}

extern "C" uint64_t process_fault_returnable64() {
    return current_process() != 0 ? 1 : 0;
}

extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    syscall_count++;

    if (syscall_no == SYS_WRITE) {
        const char* msg = (const char*)(uintptr_t)arg1;
        if (msg == 0) {
            return 0;
        }
        while (*msg) {
            putchar_both(*msg++);
        }
        return 0;
    }

    if (syscall_no == SYS_EXIT) {
        process_mark_returned(current_process(), PROCESS_TERM_EXIT, (uint32_t)arg1);
        print("\nUser mode exit requested.");
        return SYSCALL_RETURN_TO_KERNEL;
    }

    if (syscall_no == SYS_PUTCHAR) {
        putchar_both((char)(arg1 & 0xFF));
        return 1;
    }

    if (syscall_no == SYS_GETCHAR) {
        while (1) {
            char ascii = 0;
            if (keyboard.try_read_char(&ascii)) {
                return (uint64_t)(unsigned char)ascii;
            }

            if (continue_woken_processes(0)) {
                redraw_user_shell_prompt_if_needed();
                continue;
            }

            if (continue_background_processes(0)) {
                redraw_user_shell_prompt_if_needed();
                continue;
            }

            __asm__ volatile("sti; hlt; cli");
        }
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
            return (uint64_t)-1;
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

    if (syscall_no == SYS_VFS_OPEN) {
        char file_name[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        Process* owner = current_process();
        return (uint64_t)vfs_open_for_owner(file_name, (uint32_t)arg2, owner != 0 ? owner->pid : 0);
    }

    if (syscall_no == SYS_VFS_READ) {
        uint32_t requested = (uint32_t)arg3;
        if (requested == 0) {
            return 0;
        }
        if (requested > 4096) {
            requested = 4096;
        }

        uint8_t* temp = (uint8_t*)kmalloc(requested);
        if (temp == 0) {
            return (uint64_t)-1;
        }

        uint32_t bytes_read = 0;
        int result = vfs_read((int)arg1, temp, requested, &bytes_read);
        if (result != VFS_OK || !copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg2, temp, bytes_read)) {
            kfree(temp);
            return (uint64_t)-1;
        }

        kfree(temp);
        return bytes_read;
    }

    if (syscall_no == SYS_VFS_WRITE) {
        uint32_t requested = (uint32_t)arg3;
        if (requested == 0) {
            return 0;
        }
        if (requested > 4096) {
            requested = 4096;
        }

        uint8_t* temp = (uint8_t*)kmalloc(requested);
        if (temp == 0) {
            return (uint64_t)-1;
        }
        if (!copy_user_buffer((const uint8_t*)(uintptr_t)arg2, temp, requested)) {
            kfree(temp);
            return (uint64_t)-1;
        }

        uint32_t bytes_written = 0;
        int result = vfs_write((int)arg1, temp, requested, &bytes_written);
        kfree(temp);
        if (result != VFS_OK) {
            return (uint64_t)-1;
        }
        return bytes_written;
    }

    if (syscall_no == SYS_VFS_CLOSE) {
        return vfs_close((int)arg1) == VFS_OK ? 0 : (uint64_t)-1;
    }

    if (syscall_no == SYS_VFS_SEEK) {
        uint32_t position = 0;
        if (vfs_seek((int)arg1, (int32_t)arg2, (uint32_t)arg3, &position) != VFS_OK) {
            return (uint64_t)-1;
        }
        return position;
    }

    if (syscall_no == SYS_VFS_TELL) {
        uint32_t position = 0;
        if (vfs_tell((int)arg1, &position) != VFS_OK) {
            return (uint64_t)-1;
        }
        return position;
    }

    if (syscall_no == SYS_MKDIR || syscall_no == SYS_RMDIR) {
        char dir_path[USER_PATH_MAX];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, dir_path, sizeof(dir_path))) {
            print("\nInvalid user path pointer.");
            return (uint64_t)-1;
        }

        int result = syscall_no == SYS_MKDIR ? vfs_mkdir(dir_path) : vfs_rmdir(dir_path);
        if (result == VFS_OK) {
            print(syscall_no == SYS_MKDIR ? "\nCreated dir: " : "\nRemoved dir: ");
            print(dir_path);
            print("\n");
            return 0;
        }

        print(syscall_no == SYS_MKDIR ? "\nFailed to create dir: " : "\nFailed to remove dir: ");
        print(dir_path);
        print("\n");
        return (uint64_t)-1;
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
        print_process_table(process_table, PROCESS_TABLE_SIZE, pit.get_tick());
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
        uint32_t pid = (uint32_t)arg1;
        if (!resume_user_program(pid)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_KILL_USER) {
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
        print_jobs_for_process(process, process_table, PROCESS_TABLE_SIZE, pit.get_tick());
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
