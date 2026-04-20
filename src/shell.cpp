#include "shell.h"
#include "drivers/terminal.hpp"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80

extern char shell_buffer[];
extern int buffer_index;
extern void* notebook_ptr;

// C++ 클래스 객체들을 사용하기 위해 헤더 포함
#include "fat12.h"
#include "drivers/pit.h"
#include "drivers/ata.h"
#include "kernel.h"

// 순수 C로 작성된 헤더들은 extern "C"로 감싸서 인클루드 해야 해!
extern "C" {
    #include "heap.h"  // kmalloc, kfree, struct heap_header, heap_start를 위해 필수
}

static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;   // 지금까지 저장된 명령어 개수
static int history_index = 0;   // 방향키로 탐색 중인 현재 위치

// kernel.cpp에 선언된 전역 객체들 가져오기
extern Terminal terminal;
extern FAT12Driver fat;
extern PIT pit;
extern ATADriver ata;

char* get_argument(char* input) {
    while (*input != ' ' && *input != '\0') input++;
    if (*input == ' ') return input + 1;
    return 0;
}

void dump_heap() {
    struct heap_header* curr = heap_start;
    terminal.print("\n=== HEAP MAP ===");
    while (curr != 0) {
        terminal.print("\n[");
        terminal.print_hex((uint32_t)curr);
        terminal.print("] Size:");
        terminal.print_hex(curr->size);
        terminal.print(" Free:");
        terminal.print_hex(curr->is_free);
        curr = curr->next;
    }
    terminal.print("\n================");
}

void execute_command() {
    shell_buffer[buffer_index] = '\0';
    shell_save_history();

    char* arg = get_argument(shell_buffer);

    char cmd[32];
    int i = 0;
    while (shell_buffer[i] != ' ' && shell_buffer[i] != '\0' && i < 31) {
        cmd[i] = shell_buffer[i];
        i++;
    }
    cmd[i] = '\0';

    if (strcmp(cmd, "dump") == 0) {
        dump_heap();
    }
    else if (strcmp(cmd, "write") == 0) {
        if (!arg) { terminal.print("\nUsage: write [message]"); }
        else {
            int len = strlen(arg);

            if (notebook_ptr != 0) {
                kfree(notebook_ptr);
            }
            void* new_ptr = kmalloc(len + 1);
            notebook_ptr = new_ptr;

            if (new_ptr == 0) {
                terminal.print("\nOut of memory!");
                return;
            }

            char* dest = (char*)notebook_ptr;
            int i = 0;
            while (arg[i] != '\0' && i < 4095) {
                dest[i] = arg[i];
                i++;
            }
            dest[i] = '\0';
        }
    }
    else if (strcmp(cmd, "read") == 0) {
        if (notebook_ptr == 0) { terminal.print("\nNotebook is empty."); }
        else {
            terminal.print("\nContent: ");
            terminal.print((char*)notebook_ptr);
        }
    }
    else if (strcmp(cmd, "free") == 0) {
        if (notebook_ptr != 0) {
            kfree(notebook_ptr);
            terminal.print("\nMemory Freed at ");
            terminal.print_hex((uint32_t)notebook_ptr);
            notebook_ptr = 0;
        } else {
            terminal.print("\nNothing to free.");
        }
    }
    else if (strcmp(cmd, "help") == 0) {
        terminal.print("\nAvailable commands: help, clear, version, write, read, free, dump, atatest, ls, load, save, rm, pagefault, uptime");
    }
    else if (strcmp(cmd, "clear") == 0) {
        terminal.clear();
    }
    else if (strcmp(cmd, "version") == 0) {
        terminal.print("\n[OS-Kernel] v0.0.2 (32-bit Protected Mode, C++)");
        terminal.print("\nx86 ASM BootLoader");
    }
    else if (strcmp(cmd, "atatest") == 0) {
        uint8_t buffer[512];
        ata.read_sector(0, buffer);
        terminal.print("\nSector 0:");
        for (int i = 0; i < 16; i++) {
            terminal.print_hex(buffer[i]);
            terminal.print(" ");
        }
    }
    else if (strcmp(cmd, "ls") == 0) {
        fat.list_files();
    }
    else if (strcmp(cmd, "load") == 0) {
        if (!arg) { terminal.print("\nUsage: load [filename]"); }
        else {
            // 8.3 형식으로 변환 (예: "hello.txt" → "HELLO   TXT")
            char name83[11];
            for (int i = 0; i < 11; i++) name83[i] = ' ';
            
            int i = 0, j = 0;
            while (arg[i] != '.' && arg[i] != '\0' && j < 8) {
                name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
                i++;
            }
            
            if (arg[i] == '.') i++;
            j = 8;
            while (arg[i] != '\0' && j < 11) {
                name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
                i++;
            }

            DirEntry entry;
            if (fat.find_file(name83, &entry)) {
                // 수정
                uint32_t buf_size = entry.file_size + 1;
                if (buf_size < 512) buf_size = 512;
                uint8_t* buffer = (uint8_t*)kmalloc(buf_size);

                if (buffer == 0) {
                    terminal.print("\nOut of memory!");
                } else {
                    fat.read_file(&entry, buffer);
                    buffer[entry.file_size] = '\0';
                    terminal.print("\n");
                    terminal.print((char*)buffer);
                    kfree(buffer);
                }
            } else {
                terminal.print("\nFile not found.");
            }
        }
    }
    else if (strcmp(cmd, "rm") == 0) {
        if (!arg) { terminal.print("\nUsage: rm [filename]"); }
        else {
            char name83[11];
            for (int i = 0; i < 11; i++) name83[i] = ' ';
            int i = 0, j = 0;
            while (arg[i] != '.' && arg[i] != '\0' && j < 8) {
                name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
                i++;
            }
            if (arg[i] == '.') i++;
            j = 8;
            while (arg[i] != '\0' && j < 11) {
                name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
                i++;
            }

            if (fat.delete_file(name83)) {
                terminal.print("\nDeleted: ");
                terminal.print(arg);
            } else {
                terminal.print("\nFile not found.");
            }
        }
    }
    else if (strcmp(cmd, "save") == 0) {
    if (!arg) { terminal.print("\nUsage: save [filename]"); }
    else {
        // 8.3 형식 변환
        char name83[11];
        for (int i = 0; i < 11; i++) name83[i] = ' ';
        int i = 0, j = 0;
        while (arg[i] != '.' && arg[i] != '\0' && j < 8) {
            name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
            i++;
        }
        if (arg[i] == '.') i++;
        j = 8;
        while (arg[i] != '\0' && j < 11) {
            name83[j++] = arg[i] >= 'a' && arg[i] <= 'z' ? arg[i] - 32 : arg[i];
            i++;
        }

        if (notebook_ptr == 0) {
            terminal.print("\nNotebook is empty. Use write first.");
        } else {
            uint32_t size = strlen((char*)notebook_ptr);
            if (fat.write_file(name83, (uint8_t*)notebook_ptr, size)) {
                terminal.print("\nSaved: ");
                terminal.print(arg);
            } else {
                terminal.print("\nFailed to save.");
                }
            }
        }
    }
    else if (strcmp(cmd, "pagefault") == 0) {
        uint32_t* bad_ptr = (uint32_t*)0x80000000;  // 매핑 안 된 주소
        *bad_ptr = 0x1234;  // 페이지 폴트 발생!
    }
    else if (strcmp(cmd, "uptime") == 0) {
        terminal.print("\nTick: ");
        terminal.print_hex(pit.get_tick());
    }
    else if (buffer_index > 0) {
        terminal.print("\nUnknown command: ");
        terminal.print(cmd);
    }

    buffer_index = 0;
    terminal.print("\nOS-Kernel> ");
}

void shell_input(char ascii) {
    if (ascii == '\n') {
        execute_command();
    } else if (ascii == '\b') {
        if (buffer_index > 0) {
            buffer_index--;
            terminal.putchar('\b');
        }
    } else {
        if (buffer_index < MAX_BUFFER_SIZE - 1) {
            shell_buffer[buffer_index++] = ascii;
            terminal.putchar(ascii);
        }
    }
}

extern "C" void shell_recall_history(int direction) {
    if (history_count == 0) return;

    int available = history_count < MAX_HISTORY ? history_count : MAX_HISTORY;
    int oldest_index = history_count - available;
    int new_index = history_index + direction;

    if (new_index < oldest_index) new_index = oldest_index;
    if (new_index >= history_count) new_index = history_count - 1;

    if (new_index == history_index) return;
    history_index = new_index;

    while (buffer_index > 0) {
        buffer_index--;
        terminal.putchar('\b'); // 또는 putchar('\b'); 
    }

    int actual_idx = history_index % MAX_HISTORY;
    char* recalled_cmd = history[actual_idx];

    buffer_index = 0;
    for (int i = 0; recalled_cmd[i] != '\0' && buffer_index < MAX_BUFFER_SIZE - 1; i++) {
        shell_buffer[buffer_index] = recalled_cmd[i]; // 셸이 인식할 버퍼 업데이트
        terminal.putchar(recalled_cmd[i]);
        buffer_index++;
    }
    shell_buffer[buffer_index] = '\0';
}

void shell_save_history() {
    if (buffer_index == 0) return;

    int save_idx = history_count % MAX_HISTORY;
    int copy_len = buffer_index;
    if (copy_len >= MAX_CMD_LEN) {
        copy_len = MAX_CMD_LEN - 1;
    }

    for (int i = 0; i < copy_len; i++) {
        history[save_idx][i] = shell_buffer[i];
    }
    history[save_idx][copy_len] = '\0';

    history_count++;
    history_index = history_count;
}
