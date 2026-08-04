#include <os64/os64.h>
#include "userlib.h"

#define SHELLC_INPUT_MAX 160
#define SHELLC_CMDLINE_MAX 160

static char shell_input[SHELLC_INPUT_MAX];
static char shell_cwd[SHELLC_CMDLINE_MAX] = "/";
static OsProcessIdentity terminal_peer;

static void terminal_read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;
    if (buffer == 0 || capacity == 0) return;
    buffer[0] = '\0';

    while (1) {
        long value = user_getchar();
        if (value == OS_ERR_CANCELLED) {
            static const char exit_text[] = "exit";
            uint32_t copy = capacity > sizeof(exit_text)
                ? sizeof(exit_text) : capacity;
            os_memcpy(buffer, exit_text, copy);
            buffer[copy - 1u] = '\0';
            return;
        }
        if (value < 0) continue;

        char character = (char)value;
        if (character == '\r' || character == '\n') {
            user_putchar('\n');
            break;
        }
        if (character == 3) {
            user_write_cstr("^C\n");
            length = 0;
            break;
        }
        if (character == 4 && length == 0) {
            static const char exit_text[] = "exit";
            user_write_cstr("exit\n");
            os_memcpy(buffer, exit_text, sizeof(exit_text));
            return;
        }
        if (character == '\b') {
            if (length != 0) {
                buffer[--length] = '\0';
                user_write_cstr("\b \b");
            }
            continue;
        }
        if (character < 32 || character > 126 || length + 1u >= capacity)
            continue;
        buffer[length++] = character;
        buffer[length] = '\0';
        user_putchar(character);
    }
}

#define user_read_line terminal_read_line
#define USHELL_MAIN terminal_shell_loop

#include "ushell/ushell_helpers.inc"
#include "ushell/ushell_main.inc"

#undef user_read_line
#undef USHELL_MAIN

int main(int argc, char** argv) {
    if (argc != 3 || !os_parse_u32(argv[1], &terminal_peer.pid) ||
        !os_parse_u32(argv[2], &terminal_peer.generation) ||
        terminal_peer.pid == 0 || terminal_peer.generation == 0) {
        return 2;
    }

    OsTerminalPacket hello;
    os_terminal_packet_init(&hello, OS_TERMINAL_COMMAND_HELLO);
    hello.sequence = 1;
    hello.columns = 80;
    hello.rows = 24;
    if (os_terminal_send(terminal_peer, &hello, 10) < 0) return 3;
    long bind_result = os_terminal_session_bind(terminal_peer);
    if (bind_result < 0) {
        os_printf("[terminal-shell] bind failed %ld\n", bind_result);
        return 4;
    }
    /* Let the launcher mark this child as background before blocking on stdin. */
    os_sleep(1);

    int status = terminal_shell_loop();
    (void)os_terminal_session_exit(status);
    return status;
}
