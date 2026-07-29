#include <os64/os64.h>

#define PAGE_SIZE 4096u
#define WORKER_COUNT 3u
#define CHURN_CYCLES 64u

static volatile uint32_t ready_count;
static volatile uint32_t stop_workers;
static volatile uint32_t worker_failures;
static volatile uint64_t worker_reads;
static volatile uint32_t* stable_page;

static long reader_worker(void* argument) {
    const uint32_t cpu = (uint32_t)(uintptr_t)argument;
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS ||
        os_thread_set_affinity(self, 1u << cpu) != OS_SUCCESS) {
        __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
        return 0xE0u + cpu;
    }
    __atomic_add_fetch(&ready_count, 1u, __ATOMIC_RELEASE);
    uint64_t reads = 0;
    while (!__atomic_load_n(&stop_workers, __ATOMIC_ACQUIRE)) {
        const uint32_t value =
            __atomic_load_n(&stable_page[0], __ATOMIC_ACQUIRE);
        if (value != 0x46F00001u) {
            __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
            break;
        }
        reads++;
        if ((reads & 0x3FFFu) == 0) {
            os_thread_yield();
        }
    }
    __atomic_add_fetch(&worker_reads, reads, __ATOMIC_RELAXED);
    return 0x60u + cpu;
}

int main(void) {
    uint8_t* heap_base = (uint8_t*)os_brk(0);
    if (heap_base == 0 ||
        os_brk(heap_base + PAGE_SIZE) != heap_base + PAGE_SIZE) {
        os_puts("[TLBX] FAIL initial heap");
        return 1;
    }
    stable_page = (volatile uint32_t*)heap_base;
    stable_page[0] = 0x46F00001u;

    OsThreadIdentity workers[WORKER_COUNT];
    uint32_t failures = 0;
    for (uint32_t i = 0; i < WORKER_COUNT; i++) {
        if (os_thread_create(reader_worker,
                             (void*)(uintptr_t)(i + 1u),
                             OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_SUCCESS) {
            failures++;
        }
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) != WORKER_COUNT &&
           failures == 0) {
        os_thread_yield();
    }

    uint32_t completed = 0;
    for (uint32_t cycle = 0; cycle < CHURN_CYCLES && failures == 0; cycle++) {
        if (os_brk(heap_base + 2u * PAGE_SIZE) !=
            heap_base + 2u * PAGE_SIZE) {
            failures++;
            break;
        }
        volatile uint32_t* churn_page =
            (volatile uint32_t*)(heap_base + PAGE_SIZE);
        churn_page[0] = 0xA5000000u | cycle;
        if (os_brk(heap_base + PAGE_SIZE) != heap_base + PAGE_SIZE) {
            failures++;
            break;
        }
        if (stable_page[0] != 0x46F00001u) {
            failures++;
            break;
        }
        completed++;
    }

    __atomic_store_n(&stop_workers, 1u, __ATOMIC_RELEASE);
    for (uint32_t i = 0; i < WORKER_COUNT; i++) {
        uint32_t status = 0;
        if (os_thread_join(workers[i], &status) != OS_SUCCESS ||
            status != 0x61u + i) {
            failures++;
        }
    }
    failures += __atomic_load_n(&worker_failures, __ATOMIC_ACQUIRE);
    const uint64_t reads =
        __atomic_load_n(&worker_reads, __ATOMIC_ACQUIRE);
    os_printf("[TLBX] cycles=%u reads=%u failures=%u\n",
              completed,
              (uint32_t)reads,
              failures);
    if (completed == CHURN_CYCLES && reads != 0 && failures == 0) {
        os_puts("[TLBX] PASS\n");
        return 0;
    }
    os_puts("[TLBX] FAIL\n");
    return 1;
}
