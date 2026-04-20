#include <stddef.h>

inline void* operator new(size_t, void* ptr) { return ptr; }
inline void* operator new[](size_t, void* ptr) { return ptr; }

extern "C" {
    void* kmalloc(size_t size);
    void kfree(void* ptr);
    void __cxa_pure_virtual() {
        while(1) {}
    }
}

// new / delete
void* operator new(size_t size) {
    extern void* kmalloc(size_t);
    return kmalloc(size);
}

void* operator new[](size_t size) {
    extern void* kmalloc(size_t);
    return kmalloc(size);
}

void operator delete(void* ptr) {
    extern void kfree(void*);
    kfree(ptr);
}

void operator delete[](void* ptr) {
    extern void kfree(void*);
    kfree(ptr);
}

void operator delete(void* ptr, size_t) {
    extern void kfree(void*);
    kfree(ptr);
}

void operator delete[](void* ptr, size_t) {
    extern void kfree(void*);
    kfree(ptr);
}