#include <os64/os64.h>
#include "userlib.h"

#define SHELLC_INPUT_MAX 160
#define SHELLC_CMDLINE_MAX 160
#define TERMINAL_INPUT_CAPACITY 256u

static char shell_input[SHELLC_INPUT_MAX];
static char shell_cwd[SHELLC_CMDLINE_MAX] = "/";
static OsProcessIdentity terminal_peer;
static uint32_t terminal_sequence = 1;
static uint32_t terminal_last_input_sequence;
static uint32_t terminal_columns = 80;
static uint32_t terminal_rows = 24;
static uint8_t terminal_input[TERMINAL_INPUT_CAPACITY];
static uint32_t terminal_input_head;
static uint32_t terminal_input_count;
static uint32_t terminal_output_dropped;
static int terminal_hung_up;

static uint32_t next_sequence(void) {
    uint32_t sequence = terminal_sequence++;
    if (terminal_sequence == 0) terminal_sequence = 1;
    return sequence == 0 ? next_sequence() : sequence;
}

static long send_packet(uint32_t command,
                        const uint8_t* bytes,
                        uint32_t length,
                        int32_t status) {
    OsTerminalPacket packet;
    os_terminal_packet_init(&packet, command);
    packet.sequence = next_sequence();
    packet.columns = terminal_columns;
    packet.rows = terminal_rows;
    packet.status = status;
    packet.length = length;
    if (terminal_output_dropped != 0 && command == OS_TERMINAL_COMMAND_OUTPUT)
        packet.flags |= OS_TERMINAL_FLAG_OVERFLOW;
    if (length != 0) os_memcpy(packet.data, bytes, length);
    long result = os_terminal_send(terminal_peer, &packet, 50);
    if (result >= 0 && command == OS_TERMINAL_COMMAND_OUTPUT)
        terminal_output_dropped = 0;
    return result;
}

static long terminal_write_bytes(const char* text, uint32_t length) {
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t chunk = length - offset;
        if (chunk > OS_TERMINAL_PACKET_DATA_MAX)
            chunk = OS_TERMINAL_PACKET_DATA_MAX;
        long result = send_packet(OS_TERMINAL_COMMAND_OUTPUT,
                                  (const uint8_t*)text + offset, chunk, 0);
        if (result < 0) {
            terminal_output_dropped += length - offset;
            return result;
        }
        offset += chunk;
    }
    return (long)length;
}

static long terminal_write_cstr(const char* text) {
    return text == 0 ? OS_ERR_INVALID_ARGUMENT
                     : terminal_write_bytes(text, (uint32_t)os_strlen(text));
}

static void terminal_puts(const char* text) {
    terminal_write_cstr(text);
    terminal_write_cstr("\n");
}

static long terminal_putchar(char character) {
    return terminal_write_bytes(&character, 1);
}

static void terminal_print_u32(uint32_t value, uint32_t base, int negative) {
    char digits[16];
    uint32_t count = 0;
    if (negative) terminal_putchar('-');
    if (value == 0) {
        terminal_putchar('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        uint32_t digit = value % base;
        digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    }
    while (count != 0) terminal_putchar(digits[--count]);
}

static void terminal_vprintf(const char* format, va_list arguments) {
    while (format != 0 && *format != '\0') {
        if (*format != '%') {
            terminal_putchar(*format++);
            continue;
        }
        format++;
        if (*format == '%') {
            terminal_putchar('%');
        } else if (*format == 's') {
            const char* text = va_arg(arguments, const char*);
            terminal_write_cstr(text != 0 ? text : "(null)");
        } else if (*format == 'c') {
            terminal_putchar((char)va_arg(arguments, int));
        } else if (*format == 'u') {
            terminal_print_u32(va_arg(arguments, uint32_t), 10, 0);
        } else if (*format == 'x') {
            terminal_print_u32(va_arg(arguments, uint32_t), 16, 0);
        } else if (*format == 'd') {
            int32_t value = va_arg(arguments, int32_t);
            uint32_t magnitude = value < 0
                ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
            terminal_print_u32(magnitude, 10, value < 0);
        } else {
            terminal_putchar('%');
            if (*format != '\0') terminal_putchar(*format);
        }
        if (*format != '\0') format++;
    }
}

static void terminal_printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    terminal_vprintf(format, arguments);
    va_end(arguments);
}

static long terminal_clear_screen(void) {
    return terminal_write_cstr("\x1b[2J\x1b[H");
}

static int queue_input(const uint8_t* bytes, uint32_t length) {
    if (length > TERMINAL_INPUT_CAPACITY - terminal_input_count) return 0;
    uint32_t tail = (terminal_input_head + terminal_input_count) %
                    TERMINAL_INPUT_CAPACITY;
    for (uint32_t i = 0; i < length; i++) {
        terminal_input[tail] = bytes[i];
        tail = (tail + 1u) % TERMINAL_INPUT_CAPACITY;
    }
    terminal_input_count += length;
    return 1;
}

static int receive_control(void) {
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE;
    filter.sender_pid = terminal_peer.pid;
    filter.sender_generation = terminal_peer.generation;
    filter.type = OS_IPC_MESSAGE_EVENT;
    while (!terminal_hung_up && terminal_input_count == 0) {
        OsIpcMessageV2 message;
        long result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_ERR_WOULD_BLOCK) {
            if (os_process_identity_alive(terminal_peer) < 0) {
                terminal_hung_up = 1;
                break;
            }
            os_sleep(1);
            continue;
        }
        if (result < 0 || message.length != sizeof(OsTerminalPacket)) continue;
        OsTerminalPacket packet;
        os_memcpy(&packet, message.payload, sizeof(packet));
        if (os_terminal_packet_validate(&packet) < 0 ||
            packet.sequence <= terminal_last_input_sequence) continue;
        terminal_last_input_sequence = packet.sequence;
        if (packet.command == OS_TERMINAL_COMMAND_INPUT) {
            if (!queue_input(packet.data, packet.length)) {
                terminal_write_cstr("\n[input overflow]\n");
            }
        } else if (packet.command == OS_TERMINAL_COMMAND_RESIZE) {
            terminal_columns = packet.columns;
            terminal_rows = packet.rows;
        } else if (packet.command == OS_TERMINAL_COMMAND_HANGUP) {
            terminal_hung_up = 1;
        }
    }
    return !terminal_hung_up;
}

static long terminal_getchar(void) {
    if (terminal_input_count == 0 && !receive_control()) return OS_ERR_CANCELLED;
    if (terminal_input_count == 0) return OS_ERR_WOULD_BLOCK;
    uint8_t byte = terminal_input[terminal_input_head];
    terminal_input_head = (terminal_input_head + 1u) % TERMINAL_INPUT_CAPACITY;
    terminal_input_count--;
    return byte;
}

static size_t terminal_read_line(char* buffer, size_t capacity) {
    uint32_t length = 0;
    if (buffer == 0 || capacity == 0) return 0;
    buffer[0] = '\0';
    while (!terminal_hung_up) {
        long value = terminal_getchar();
        if (value < 0) continue;
        char character = (char)value;
        if (character == '\r' || character == '\n') {
            terminal_putchar('\n');
            break;
        }
        if (character == 3) {
            terminal_write_cstr("^C\n");
            length = 0;
            break;
        }
        if (character == 4 && length == 0) {
            terminal_write_cstr("exit\n");
            const char exit_text[] = "exit";
            os_memcpy(buffer, exit_text, sizeof(exit_text));
            return sizeof(exit_text) - 1u;
        }
        if (character == '\b') {
            if (length != 0) {
                buffer[--length] = '\0';
                terminal_write_cstr("\b \b");
            }
            continue;
        }
        if (character < 32 || character > 126 || length + 1u >= capacity)
            continue;
        buffer[length++] = character;
        buffer[length] = '\0';
        terminal_putchar(character);
    }
    if (terminal_hung_up) {
        const char exit_text[] = "exit";
        uint32_t copy = capacity > sizeof(exit_text) ? sizeof(exit_text) : capacity;
        os_memcpy(buffer, exit_text, copy);
        buffer[copy - 1u] = '\0';
        return copy - 1u;
    }
    return length;
}

#define user_write_cstr terminal_write_cstr
#define user_puts terminal_puts
#define user_putchar terminal_putchar
#define user_getchar terminal_getchar
#define user_printf terminal_printf
#define user_read_line terminal_read_line
#define user_clear_screen terminal_clear_screen
#define USHELL_MAIN terminal_shell_loop

#include "ushell/ushell_helpers.inc"
#include "ushell/ushell_main.inc"

#undef user_write_cstr
#undef user_puts
#undef user_putchar
#undef user_getchar
#undef user_printf
#undef user_read_line
#undef user_clear_screen
#undef USHELL_MAIN

int main(int argc, char** argv) {
    if (argc != 3 || !os_parse_u32(argv[1], &terminal_peer.pid) ||
        !os_parse_u32(argv[2], &terminal_peer.generation) ||
        terminal_peer.pid == 0 || terminal_peer.generation == 0) {
        return 2;
    }
    OsTerminalPacket hello;
    os_terminal_packet_init(&hello, OS_TERMINAL_COMMAND_HELLO);
    hello.sequence = next_sequence();
    hello.columns = terminal_columns;
    hello.rows = terminal_rows;
    if (os_terminal_send(terminal_peer, &hello, 10) < 0) return 3;

    int status = terminal_shell_loop();
    OsTerminalPacket exited;
    os_terminal_packet_init(&exited, OS_TERMINAL_COMMAND_EXIT);
    exited.sequence = next_sequence();
    exited.columns = terminal_columns;
    exited.rows = terminal_rows;
    exited.status = status;
    (void)os_terminal_send(terminal_peer, &exited, 50);
    return status;
}
