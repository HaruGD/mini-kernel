#include "userlib.h"

#define SHELLC_INPUT_MAX 64

static int str_eq(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int str_startswith(const char* text, const char* prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static uint32_t parse_u32(const char* text) {
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = (value * 10u) + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

static void print_prompt(void) {
    user_write_cstr("csh> ");
}

static void print_help(void) {
    user_write_cstr(
        "Commands: help, exit, ls, jobs, ps, wait, laststatus\n"
        "          run [file], sleep [ticks]\n");
}

static void read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;

    if (capacity == 0) {
        return;
    }

    while (1) {
        long ch = user_getchar();
        if (ch < 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            user_putchar('\n');
            break;
        }

        if (ch == '\b') {
            if (length > 0) {
                length--;
                buffer[length] = '\0';
                user_putchar('\b');
                user_putchar(' ');
                user_putchar('\b');
            }
            continue;
        }

        if (ch < 32 || ch > 126) {
            continue;
        }

        if (length + 1 >= capacity) {
            continue;
        }

        buffer[length++] = (char)ch;
        buffer[length] = '\0';
        user_putchar((char)ch);
    }

    buffer[length] = '\0';
}

int user_main(void) {
    char input[SHELLC_INPUT_MAX];

    user_write_cstr(
        "=== USHELL_C.ELF ===\n"
        "C user shell ready. Type help for commands.\n");

    while (1) {
        print_prompt();
        read_line(input, sizeof(input));

        if (input[0] == '\0') {
            continue;
        }

        if (str_eq(input, "help")) {
            print_help();
            continue;
        }

        if (str_eq(input, "exit")) {
            user_write_cstr("Leaving C user shell...\n");
            return 0;
        }

        if (str_eq(input, "ls")) {
            user_list_files();
            continue;
        }

        if (str_eq(input, "jobs")) {
            user_jobs();
            continue;
        }

        if (str_eq(input, "ps")) {
            user_ps();
            continue;
        }

        if (str_eq(input, "wait")) {
            user_wait();
            continue;
        }

        if (str_eq(input, "laststatus")) {
            user_laststatus();
            continue;
        }

        if (str_startswith(input, "run ")) {
            if (input[4] == '\0') {
                user_write_cstr("Usage: run [file]\n");
                continue;
            }
            if (user_run(&input[4]) < 0) {
                user_write_cstr("run failed.\n");
            }
            continue;
        }

        if (str_startswith(input, "sleep ")) {
            uint32_t ticks;
            if (input[6] == '\0') {
                user_write_cstr("Usage: sleep [ticks]\n");
                continue;
            }
            ticks = parse_u32(&input[6]);
            if (ticks == 0) {
                user_write_cstr("sleep requires a positive tick count.\n");
                continue;
            }
            user_sleep(ticks);
            continue;
        }

        user_write_cstr("Unknown command: ");
        user_write_cstr(input);
        user_write_cstr("\n");
    }
}
