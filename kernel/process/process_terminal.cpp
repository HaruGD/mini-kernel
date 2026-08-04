#include "kernel/process_terminal.h"

#include "kernel/ipc/ipc.h"
#include "kernel/kutil64.h"
#include "kernel/mm/heap.h"
#include "kernel/process64.h"
#include "kernel/syscall64.h"
#include "os64/terminal_types.h"

static Process* terminal_owner(const Process* process) {
    if (!process_terminal_attached(process)) {
        return 0;
    }
    ProcessIdentity identity = {
        process->terminal_owner_pid,
        process->terminal_owner_generation,
    };
    return find_process_by_identity(identity);
}

static Process* terminal_peer(Process* owner) {
    if (owner == 0 || owner->terminal_peer_pid == 0 ||
        owner->terminal_peer_generation == 0) {
        return 0;
    }
    ProcessIdentity identity = {
        owner->terminal_peer_pid,
        owner->terminal_peer_generation,
    };
    return find_process_by_identity(identity);
}

static uint32_t next_output_sequence(Process* owner) {
    uint32_t sequence = __atomic_add_fetch(&owner->terminal_output_sequence,
                                           1u, __ATOMIC_RELAXED);
    if (sequence == 0) {
        __atomic_store_n(&owner->terminal_output_sequence, 1u,
                         __ATOMIC_RELAXED);
        sequence = 1;
    }
    return sequence;
}

static int send_packet(Process* owner,
                       uint32_t command,
                       const uint8_t* bytes,
                       uint32_t length,
                       int32_t status) {
    Process* peer = terminal_peer(owner);
    if (owner == 0 || peer == 0) {
        return SYS_ERR_CANCELLED;
    }
    OsTerminalPacket packet = {};
    packet.size = sizeof(packet);
    packet.abi_version = OS64_TERMINAL_ABI_VERSION;
    packet.command = command;
    packet.sequence = next_output_sequence(owner);
    packet.columns = owner->terminal_columns;
    packet.rows = owner->terminal_rows;
    packet.length = length;
    packet.status = status;
    if (command == OS_TERMINAL_COMMAND_OUTPUT &&
        __atomic_load_n(&owner->terminal_output_dropped, __ATOMIC_RELAXED) != 0) {
        packet.flags |= OS_TERMINAL_FLAG_OVERFLOW;
    }
    for (uint32_t i = 0; i < length; i++) {
        packet.data[i] = bytes[i];
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&owner->terminal_output_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    if (owner->terminal_output_count == PROCESS_TERMINAL_OUTPUT_CAPACITY) {
        kernel_spinlock_release(&owner->terminal_output_lock, &token);
        return SYS_ERR_QUEUE_FULL;
    }
    uint32_t tail = (owner->terminal_output_head +
                     owner->terminal_output_count) %
                    PROCESS_TERMINAL_OUTPUT_CAPACITY;
    if (owner->terminal_output == 0) {
        kernel_spinlock_release(&owner->terminal_output_lock, &token);
        return SYS_ERR_NOT_READY;
    }
    owner->terminal_output[tail] = packet;
    owner->terminal_output_count++;
    if (command == OS_TERMINAL_COMMAND_OUTPUT) {
        __atomic_store_n(&owner->terminal_output_dropped, 0u, __ATOMIC_RELAXED);
    }
    kernel_spinlock_release(&owner->terminal_output_lock, &token);
    return 0;
}

int process_terminal_bind(Process* process, ProcessIdentity peer) {
    if (process == 0 || process->pid == 0 || !process->active ||
        peer.pid == 0 || peer.generation == 0 ||
        process->parent_pid != peer.pid ||
        process->parent_generation != peer.generation ||
        !process_has_permissions(process, OS_PROCESS_PERMISSION_IPC)) {
        return SYS_ERR_PERMISSION_DENIED;
    }
    if (find_process_by_identity(peer) == 0) {
        return SYS_ERR_NO_TARGET;
    }
    if (process->terminal_owner_pid != 0) {
        return SYS_ERR_ALREADY_EXISTS;
    }
    process->terminal_owner_pid = process->pid;
    process->terminal_owner_generation = process->generation;
    process->terminal_peer_pid = peer.pid;
    process->terminal_peer_generation = peer.generation;
    process->terminal_output_sequence = 1u; /* HELLO is sequence 1. */
    process->terminal_last_input_sequence = 0;
    process->terminal_columns = 80;
    process->terminal_rows = 24;
    process->terminal_input_head = 0;
    process->terminal_input_count = 0;
    process->terminal_output_dropped = 0;
    process->terminal_hung_up = 0;
    process->terminal_output = (OsTerminalPacket*)kmalloc(
        sizeof(OsTerminalPacket) * PROCESS_TERMINAL_OUTPUT_CAPACITY);
    if (process->terminal_output == 0) {
        process->terminal_owner_pid = 0;
        process->terminal_owner_generation = 0;
        return SYS_ERR_OUT_OF_MEMORY;
    }
    process->terminal_output_head = 0;
    process->terminal_output_count = 0;
    process->terminal_output_cookie = PROCESS_TERMINAL_OUTPUT_COOKIE;
    return 0;
}

int process_terminal_attached(const Process* process) {
    return process != 0 && process->terminal_owner_pid != 0 &&
           process->terminal_owner_generation != 0;
}

void process_terminal_inherit(Process* child, const Process* parent) {
    if (child == 0 || !process_terminal_attached(parent)) {
        return;
    }
    child->terminal_owner_pid = parent->terminal_owner_pid;
    child->terminal_owner_generation = parent->terminal_owner_generation;
    child->terminal_peer_pid = parent->terminal_peer_pid;
    child->terminal_peer_generation = parent->terminal_peer_generation;
}

int process_terminal_write(Process* process, const uint8_t* bytes, uint32_t length) {
    Process* owner = terminal_owner(process);
    if (owner == 0 || bytes == 0) {
        return SYS_ERR_NOT_READY;
    }
    uint32_t written = 0;
    while (written < length) {
        uint32_t chunk = length - written;
        if (chunk > OS_TERMINAL_PACKET_DATA_MAX) {
            chunk = OS_TERMINAL_PACKET_DATA_MAX;
        }
        int result = send_packet(owner, OS_TERMINAL_COMMAND_OUTPUT,
                                 bytes + written, chunk, 0);
        if (result != IPC_OK) {
            __atomic_add_fetch(&owner->terminal_output_dropped,
                               length - written, __ATOMIC_RELAXED);
            return written != 0 ? (int)written : result;
        }
        written += chunk;
    }
    return (int)written;
}

static int terminal_packet_valid(const OsTerminalPacket* packet) {
    return packet != 0 && packet->size == sizeof(*packet) &&
           packet->abi_version == OS64_TERMINAL_ABI_VERSION &&
           packet->sequence != 0 &&
           packet->length <= OS_TERMINAL_PACKET_DATA_MAX;
}

static int refill_input(Process* owner) {
    OsIpcReceiveFilter filter = {};
    filter.size = sizeof(filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE;
    filter.sender_pid = owner->terminal_peer_pid;
    filter.sender_generation = owner->terminal_peer_generation;
    filter.type = OS_IPC_MESSAGE_EVENT;

    while (!owner->terminal_hung_up && owner->terminal_input_count == 0) {
        OsIpcMessageV2 message;
        int result = ipc_receive_message_v2_match(owner, &filter, &message);
        if (result != IPC_OK) {
            if (terminal_peer(owner) == 0) {
                owner->terminal_hung_up = 1;
                return SYS_ERR_CANCELLED;
            }
            return SYS_ERR_WOULD_BLOCK;
        }
        if (message.handle_count != 0 ||
            message.length != sizeof(OsTerminalPacket)) {
            continue;
        }
        OsTerminalPacket packet;
        const uint8_t* source = message.payload;
        uint8_t* target = (uint8_t*)&packet;
        for (uint32_t i = 0; i < sizeof(packet); i++) {
            target[i] = source[i];
        }
        if (!terminal_packet_valid(&packet) ||
            packet.sequence <= owner->terminal_last_input_sequence) {
            continue;
        }
        owner->terminal_last_input_sequence = packet.sequence;
        if (packet.command == OS_TERMINAL_COMMAND_INPUT && packet.length != 0) {
            uint32_t available = PROCESS_TERMINAL_INPUT_CAPACITY -
                                 owner->terminal_input_count;
            uint32_t copy = packet.length < available ? packet.length : available;
            uint32_t tail = (owner->terminal_input_head +
                             owner->terminal_input_count) %
                            PROCESS_TERMINAL_INPUT_CAPACITY;
            for (uint32_t i = 0; i < copy; i++) {
                owner->terminal_input[tail] = packet.data[i];
                tail = (tail + 1u) % PROCESS_TERMINAL_INPUT_CAPACITY;
            }
            owner->terminal_input_count += copy;
        } else if (packet.command == OS_TERMINAL_COMMAND_RESIZE &&
                   packet.columns >= OS_TERMINAL_MIN_COLUMNS &&
                   packet.columns <= OS_TERMINAL_MAX_COLUMNS &&
                   packet.rows >= OS_TERMINAL_MIN_ROWS &&
                   packet.rows <= OS_TERMINAL_MAX_ROWS) {
            owner->terminal_columns = packet.columns;
            owner->terminal_rows = packet.rows;
        } else if (packet.command == OS_TERMINAL_COMMAND_HANGUP) {
            owner->terminal_hung_up = 1;
            return SYS_ERR_CANCELLED;
        }
    }
    return owner->terminal_hung_up ? SYS_ERR_CANCELLED : 0;
}

int process_terminal_read_char(Process* process, uint8_t* character) {
    Process* owner = terminal_owner(process);
    if (owner == 0 || character == 0) {
        return SYS_ERR_NOT_READY;
    }
    int result = refill_input(owner);
    if (result < 0) {
        return result;
    }
    if (owner->terminal_input_count == 0) {
        return SYS_ERR_WOULD_BLOCK;
    }
    *character = owner->terminal_input[owner->terminal_input_head];
    owner->terminal_input_head = (owner->terminal_input_head + 1u) %
                                 PROCESS_TERMINAL_INPUT_CAPACITY;
    owner->terminal_input_count--;
    return 1;
}

int process_terminal_clear(Process* process) {
    static const uint8_t clear_sequence[] = "\x1b[2J\x1b[H";
    return process_terminal_write(process, clear_sequence,
                                  sizeof(clear_sequence) - 1u);
}

int process_terminal_send_exit(Process* process, int32_t status) {
    Process* owner = terminal_owner(process);
    if (owner == 0 || owner != process) {
        return SYS_ERR_PERMISSION_DENIED;
    }
    return send_packet(owner, OS_TERMINAL_COMMAND_EXIT, 0, 0, status);
}

int process_terminal_read_output(Process* peer,
                                 ProcessIdentity owner_identity,
                                 OsTerminalPacket* packet) {
    if (peer == 0 || packet == 0 || owner_identity.pid == 0 ||
        owner_identity.generation == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    Process* owner = find_process_by_identity(owner_identity);
    if (owner == 0) {
        return SYS_ERR_NO_TARGET;
    }
    if (owner->terminal_peer_pid != peer->pid ||
        owner->terminal_peer_generation != peer->generation) {
        return SYS_ERR_PERMISSION_DENIED;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&owner->terminal_output_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    if (owner->terminal_output == 0 || owner->terminal_output_count == 0) {
        kernel_spinlock_release(&owner->terminal_output_lock, &token);
        return SYS_ERR_WOULD_BLOCK;
    }
    *packet = owner->terminal_output[owner->terminal_output_head];
    owner->terminal_output_head = (owner->terminal_output_head + 1u) %
                                  PROCESS_TERMINAL_OUTPUT_CAPACITY;
    owner->terminal_output_count--;
    kernel_spinlock_release(&owner->terminal_output_lock, &token);
    return 0;
}

int process_terminal_close(Process* peer, ProcessIdentity owner_identity) {
    if (peer == 0) return SYS_ERR_INVALID_ARGUMENT;
    Process* owner = find_process_by_identity(owner_identity);
    if (owner == 0) return SYS_ERR_NO_TARGET;
    if (owner->terminal_peer_pid != peer->pid ||
        owner->terminal_peer_generation != peer->generation) {
        return SYS_ERR_PERMISSION_DENIED;
    }
    if (owner->active) return SYS_ERR_NOT_READY;

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&owner->terminal_output_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    OsTerminalPacket* output = owner->terminal_output;
    owner->terminal_output = 0;
    owner->terminal_output_head = 0;
    owner->terminal_output_count = 0;
    owner->terminal_output_cookie = 0;
    kernel_spinlock_release(&owner->terminal_output_lock, &token);
    if (output != 0) kfree(output);
    return 0;
}

void process_terminal_notify_message(Process* target) {
    if (target == 0 || target->terminal_owner_pid != target->pid ||
        target->terminal_owner_generation != target->generation) {
        return;
    }
    /* Only the newest foreground descendant may consume the shared stream. */
    Process* selected = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (!process->active ||
            process->terminal_owner_pid != target->pid ||
            process->terminal_owner_generation != target->generation) {
            continue;
        }
        if (selected == 0 || process->pid > selected->pid) {
            selected = process;
        }
    }
    while (selected != 0) {
        if (process_wait_signal(selected, PROCESS_WAIT_CHAR, PROCESS_WAIT_OK)) {
            break;
        }
        Process* next = 0;
        for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
            Process* process = &process_table[i];
            if (process->active && process->pid < selected->pid &&
                process->terminal_owner_pid == target->pid &&
                process->terminal_owner_generation == target->generation &&
                (next == 0 || process->pid > next->pid)) {
                next = process;
            }
        }
        selected = next;
    }
}

void process_terminal_print(const char* text) {
    if (text == 0) return;
    Process* process = current_process();
    if (!process_terminal_attached(process)) {
        print(text);
        return;
    }
    uint32_t length = 0;
    while (text[length] != '\0') length++;
    (void)process_terminal_write(process, (const uint8_t*)text, length);
}

static void process_terminal_print_hex(uint64_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (uint32_t i = 0; i < digits; i++) {
        uint32_t shift = (digits - i - 1u) * 4u;
        text[2u + i] = hex[(value >> shift) & 0xFu];
    }
    text[2u + digits] = '\0';
    process_terminal_print(text);
}

void process_terminal_print_hex32(uint32_t value) {
    process_terminal_print_hex(value, 8);
}

void process_terminal_print_hex64(uint64_t value) {
    process_terminal_print_hex(value, 16);
}
