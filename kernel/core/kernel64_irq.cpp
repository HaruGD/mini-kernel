extern "C" void keyboard_handler64() {
    keyboard.handle();
    driver_irq_dispatch(1);
}

extern "C" int user_input_active64() {
    return user_input_mode;
}

extern "C" void keyboard_deliver_char64(char ascii) {
    if (user_input_mode) {
        return;
    }

    shell_input(ascii);
}

const BootInfo* kernel_boot_info() {
    return g_boot_info;
}

uint64_t kernel_boot_tsc() {
    return boot_tsc;
}

uint32_t kernel_user_test_count() {
    return user_test_count;
}

extern "C" uint64_t timer_handler64() {
    pit.handle();
    driver_irq_dispatch(0);
    scheduler_wake_sleeping_processes(pit.get_tick());
    scheduler_on_tick();
    if (scheduler_should_preempt_current()) {
        return TIMER_PREEMPT_TO_KERNEL;
    }
    Process* process = current_process();
    if (process != 0 && process->timeslice_ticks == 0) {
        process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    }
    return 0;
}

extern "C" void user_test_interrupt_handler64() {
    user_test_count++;
    print("\nUser mode reached.");
}

extern "C" void user_exit_interrupt_handler64() {
    print("\nUser mode exit requested.");
}
