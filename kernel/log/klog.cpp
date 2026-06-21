#include "kernel/klog.h"
#include "kernel/kutil64.h"

static char log_buffer[KLOG_BUFFER_SIZE];
static uint32_t log_start = 0;
static uint32_t log_count = 0;
static uint64_t log_written = 0;
static uint64_t log_dropped = 0;
static int capture_enabled = 1;

static uint64_t irq_save() {
    uint64_t flags = 0;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static const char* level_name(uint32_t level) {
    if (level == KLOG_DEBUG) {
        return "DEBUG";
    }
    if (level == KLOG_WARN) {
        return "WARN";
    }
    if (level == KLOG_ERROR) {
        return "ERROR";
    }
    if (level == KLOG_FATAL) {
        return "FATAL";
    }
    return "INFO";
}

void klog_init() {
    uint64_t flags = irq_save();
    log_start = 0;
    log_count = 0;
    log_written = 0;
    log_dropped = 0;
    capture_enabled = 1;
    irq_restore(flags);
}

void klog_capture_char(char c) {
    uint64_t flags = irq_save();
    if (!capture_enabled) {
        irq_restore(flags);
        return;
    }

    uint32_t index = (log_start + log_count) % KLOG_BUFFER_SIZE;
    if (log_count == KLOG_BUFFER_SIZE) {
        log_start = (log_start + 1) % KLOG_BUFFER_SIZE;
        index = (log_start + log_count - 1) % KLOG_BUFFER_SIZE;
        log_dropped++;
    } else {
        log_count++;
    }
    log_buffer[index] = c;
    log_written++;
    irq_restore(flags);
}

void klog_set_capture_enabled(int enabled) {
    uint64_t flags = irq_save();
    capture_enabled = enabled != 0;
    irq_restore(flags);
}

void klog_write(uint32_t level, const char* subsystem, const char* message) {
    print("[");
    print(level_name(level));
    print("][");
    print(subsystem != 0 ? subsystem : "kernel");
    print("] ");
    print(message != 0 ? message : "");
    print("\n");
}

void klog_get_stats(KLogStats* stats) {
    if (stats == 0) {
        return;
    }
    uint64_t flags = irq_save();
    stats->capacity = KLOG_BUFFER_SIZE;
    stats->bytes_used = log_count;
    stats->bytes_written = log_written;
    stats->bytes_dropped = log_dropped;
    irq_restore(flags);
}

void klog_dump() {
    klog_dump_tail(log_count);
}

void klog_dump_tail(uint32_t max_bytes) {
    uint64_t flags = irq_save();
    uint32_t count = log_count < max_bytes ? log_count : max_bytes;
    uint32_t start = (log_start + log_count - count) % KLOG_BUFFER_SIZE;
    int previous_capture_state = capture_enabled;
    capture_enabled = 0;
    irq_restore(flags);
    for (uint32_t i = 0; i < count; i++) {
        putchar_both(log_buffer[(start + i) % KLOG_BUFFER_SIZE]);
    }
    flags = irq_save();
    capture_enabled = previous_capture_state;
    irq_restore(flags);
}

void klog_clear() {
    uint64_t flags = irq_save();
    log_start = 0;
    log_count = 0;
    log_dropped = 0;
    irq_restore(flags);
}
