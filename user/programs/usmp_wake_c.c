#include <os64/os64.h>

static volatile uint32_t ready_count;
static volatile uint32_t release_flag;
static volatile uint32_t observed_mask;
static volatile uint32_t active_count;
static volatile uint32_t maximum_active;
static OsMutex condition_mutex;
static OsCondition condition;
static OsSemaphore unbound_gate;

static int text_equals(const char* left, const char* right) {
    uint32_t i = 0;
    if (left == 0 || right == 0) {
        return 0;
    }
    while (left[i] != '\0' && right[i] != '\0' && left[i] == right[i]) {
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static int pin_self(uint32_t logical_cpu, OsThreadIdentity* self) {
    return os_thread_self(self) == OS_SUCCESS &&
           os_thread_set_affinity(*self, 1u << logical_cpu) == OS_SUCCESS;
}

static int observe_cpu(OsThreadIdentity self, uint32_t expected_cpu) {
    OsThreadInfo info;
    if (os_thread_get_info(self, &info) != OS_SUCCESS ||
        info.running_cpu != (int32_t)expected_cpu) {
        return 0;
    }
    __atomic_fetch_or(&observed_mask,
                      1u << expected_cpu,
                      __ATOMIC_ACQ_REL);
    return 1;
}

static void update_maximum(uint32_t value) {
    uint32_t observed = __atomic_load_n(&maximum_active, __ATOMIC_ACQUIRE);
    while (observed < value &&
           !__atomic_compare_exchange_n(&maximum_active,
                                        &observed,
                                        value,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
    }
}

static long condition_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    if (!pin_self(1, &self) ||
        os_mutex_lock(condition_mutex, OS_SYNC_WAIT_FOREVER) != OS_SUCCESS) {
        return 0xE1;
    }
    __atomic_store_n(&ready_count, 1u, __ATOMIC_RELEASE);
    while (__atomic_load_n(&release_flag, __ATOMIC_ACQUIRE) == 0) {
        if (os_condition_wait(condition,
                              condition_mutex,
                              OS_SYNC_WAIT_FOREVER) != OS_SUCCESS) {
            os_mutex_unlock(condition_mutex);
            return 0xE2;
        }
    }
    int valid = observe_cpu(self, 1);
    if (os_mutex_unlock(condition_mutex) != OS_SUCCESS || !valid) {
        return 0xE3;
    }
    return 0x61;
}

static long timer_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    __atomic_store_n(&ready_count, 1u, __ATOMIC_RELEASE);
    if (!pin_self(2, &self) ||
        os_thread_sleep(3) != OS_SUCCESS ||
        !observe_cpu(self, 2)) {
        return 0xE4;
    }
    return 0x62;
}

static long ipc_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    OsIpcMessage message;
    if (!pin_self(3, &self)) {
        return 0xE5;
    }
    __atomic_store_n(&ready_count, 1u, __ATOMIC_RELEASE);
    if (os_msg_wait(&message) != OS_SUCCESS ||
        message.type != OS_IPC_MESSAGE_EVENT ||
        message.length != 1 ||
        message.payload[0] != 0x5A ||
        !observe_cpu(self, 3)) {
        return 0xE6;
    }
    return 0x63;
}

static long input_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    OsInputEvent stale;
    if (!pin_self(1, &self)) {
        return 0xE7;
    }
    while (os_input_poll(&stale) == OS_SUCCESS) {
    }
    __atomic_store_n(&ready_count, 1u, __ATOMIC_RELEASE);
    for (uint32_t i = 0; i < 8; i++) {
        OsInputEvent event;
        if (os_input_wait(&event) != OS_SUCCESS) {
            return 0xE8;
        }
        if (event.type == OS_INPUT_EVENT_KEY &&
            event.data.key.type == OS_KEY_EVENT_DOWN &&
            event.data.key.character == 'z') {
            return observe_cpu(self, 1) ? 0x64 : 0xE9;
        }
    }
    return 0xEA;
}

static long unbound_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS) {
        return 0xEB;
    }
    __atomic_add_fetch(&ready_count, 1u, __ATOMIC_RELEASE);
    if (os_semaphore_wait(unbound_gate, OS_SYNC_WAIT_FOREVER) != OS_SUCCESS) {
        return 0xEC;
    }
    OsThreadInfo info;
    if (os_thread_get_info(self, &info) != OS_SUCCESS ||
        info.running_cpu < 0 || info.running_cpu >= 8) {
        return 0xED;
    }
    __atomic_fetch_or(&observed_mask,
                      1u << (uint32_t)info.running_cpu,
                      __ATOMIC_ACQ_REL);
    uint32_t active = __atomic_add_fetch(&active_count, 1u, __ATOMIC_ACQ_REL);
    update_maximum(active);
    volatile uint32_t spin = 0x01000000u;
    while (spin-- != 0u) {
        __asm__ volatile("" : : : "memory");
    }
    __atomic_sub_fetch(&active_count, 1u, __ATOMIC_ACQ_REL);
    return 0x65;
}

static int join_status(OsThreadIdentity thread, uint32_t expected) {
    uint32_t status = 0;
    return os_thread_join(thread, &status) == OS_SUCCESS && status == expected;
}

static int run_condition(void) {
    OsThreadIdentity worker;
    if (os_mutex_create(&condition_mutex) != OS_SUCCESS ||
        os_condition_create(&condition) != OS_SUCCESS ||
        os_thread_create(condition_worker,
                         0,
                         OS_THREAD_STACK_DEFAULT,
                         &worker) != OS_SUCCESS) {
        return 0;
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) == 0) {
        os_thread_yield();
    }
    if (os_mutex_lock(condition_mutex, OS_SYNC_WAIT_FOREVER) != OS_SUCCESS) {
        return 0;
    }
    __atomic_store_n(&release_flag, 1u, __ATOMIC_RELEASE);
    int broadcast_ok = os_condition_broadcast(condition) == OS_SUCCESS;
    int unlock_ok = os_mutex_unlock(condition_mutex) == OS_SUCCESS;
    int ok = broadcast_ok && unlock_ok &&
             join_status(worker, 0x61) &&
             os_condition_destroy(condition) == OS_SUCCESS &&
             os_mutex_destroy(condition_mutex) == OS_SUCCESS;
    os_printf("[SMPW] condition cpu=1 mask=%u %s\n",
              (uint32_t)observed_mask,
              ok ? "PASS" : "FAIL");
    return ok;
}

static int run_timer(void) {
    OsThreadIdentity worker;
    int ok = os_thread_create(timer_worker,
                              0,
                              OS_THREAD_STACK_DEFAULT,
                              &worker) == OS_SUCCESS &&
             join_status(worker, 0x62);
    os_printf("[SMPW] timer cpu=2 mask=%u %s\n",
              (uint32_t)observed_mask,
              ok ? "PASS" : "FAIL");
    return ok;
}

static int run_ipc(void) {
    OsThreadIdentity worker;
    if (os_thread_create(ipc_worker,
                         0,
                         OS_THREAD_STACK_DEFAULT,
                         &worker) != OS_SUCCESS) {
        return 0;
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) == 0) {
        os_thread_yield();
    }
    os_thread_sleep(2);
    int ok = os_run("usmp_sender_c.elf") == OS_SUCCESS &&
             join_status(worker, 0x63);
    os_printf("[SMPW] ipc cpu=3 mask=%u %s\n",
              (uint32_t)observed_mask,
              ok ? "PASS" : "FAIL");
    return ok;
}

static int run_input(void) {
    OsThreadIdentity worker;
    if (os_thread_create(input_worker,
                         0,
                         OS_THREAD_STACK_DEFAULT,
                         &worker) != OS_SUCCESS) {
        return 0;
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) == 0) {
        os_thread_yield();
    }
    os_puts("[SMPW] input ready\n");
    int ok = join_status(worker, 0x64);
    os_printf("[SMPW] input cpu=1 mask=%u %s\n",
              (uint32_t)observed_mask,
              ok ? "PASS" : "FAIL");
    return ok;
}

static int run_unbound(void) {
    OsThreadIdentity workers[3];
    if (os_semaphore_create(0, 3, &unbound_gate) != OS_SUCCESS) {
        return 0;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (os_thread_create(unbound_worker,
                             0,
                             OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_SUCCESS) {
            return 0;
        }
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) != 3) {
        os_thread_yield();
    }
    if (os_semaphore_post(unbound_gate, 3) != OS_SUCCESS) {
        return 0;
    }
    int ok = 1;
    for (uint32_t i = 0; i < 3; i++) {
        if (!join_status(workers[i], 0x65)) {
            ok = 0;
        }
    }
    uint32_t mask = __atomic_load_n(&observed_mask, __ATOMIC_ACQUIRE);
    uint32_t cpu_count = 0;
    for (uint32_t bit = 0; bit < 8; bit++) {
        cpu_count += (mask >> bit) & 1u;
    }
    if (cpu_count < 2 || maximum_active < 2 ||
        os_semaphore_destroy(unbound_gate) != OS_SUCCESS) {
        ok = 0;
    }
    os_printf("[SMPW] unbound mask=%u cpus=%u max_active=%u %s\n",
              mask,
              cpu_count,
              (uint32_t)maximum_active,
              ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == 0) {
        os_puts("[SMPW] mode required\n");
        return 1;
    }
    if (text_equals(argv[1], "condition")) {
        return run_condition() ? 0 : 1;
    }
    if (text_equals(argv[1], "timer")) {
        return run_timer() ? 0 : 1;
    }
    if (text_equals(argv[1], "ipc")) {
        return run_ipc() ? 0 : 1;
    }
    if (text_equals(argv[1], "input")) {
        return run_input() ? 0 : 1;
    }
    if (text_equals(argv[1], "unbound")) {
        return run_unbound() ? 0 : 1;
    }
    os_puts("[SMPW] unknown mode\n");
    return 1;
}
