#ifndef KERNEL_KLOG_H
#define KERNEL_KLOG_H

#include <stdint.h>

#define KLOG_BUFFER_SIZE 16384u

enum KLogLevel : uint32_t {
    KLOG_DEBUG = 0,
    KLOG_INFO = 1,
    KLOG_WARN = 2,
    KLOG_ERROR = 3,
    KLOG_FATAL = 4,
};

struct KLogStats {
    uint32_t capacity;
    uint32_t bytes_used;
    uint64_t bytes_written;
    uint64_t bytes_dropped;
};

void klog_init();
void klog_capture_char(char c);
void klog_write(uint32_t level, const char* subsystem, const char* message);
void klog_get_stats(KLogStats* stats);
void klog_dump();
void klog_dump_tail(uint32_t max_bytes);
void klog_clear();

#endif
