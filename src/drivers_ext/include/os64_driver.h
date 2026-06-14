#ifndef OS64_DRIVER_H
#define OS64_DRIVER_H

typedef unsigned long long os64_u64;

typedef void (*os64_klog_fn)(const char* text);
typedef void* (*os64_kmalloc_fn)(os64_u64 size);
typedef void (*os64_kfree_fn)(void* ptr);

extern os64_klog_fn kernel__klog;
extern os64_kmalloc_fn kernel__kmalloc;
extern os64_kfree_fn kernel__kfree;

#define OS64_EXPORT __attribute__((used))

static inline void os64_klog(const char* text) {
    kernel__klog(text);
}

static inline void* os64_kmalloc(os64_u64 size) {
    return kernel__kmalloc(size);
}

static inline void os64_kfree(void* ptr) {
    kernel__kfree(ptr);
}

#endif
