extern "C" void keyboard_handler64() {
    interrupt_controller_eoi(1);
    keyboard.handle();
    driver_irq_dispatch(1);
}

extern "C" int user_input_active64() {
    return display_session_gui_active() || current_process() != 0 ||
           process_focused() != 0;
}

extern "C" void keyboard_deliver_char64(char ascii) {
    (void)ascii;
    // Raw keyboard events are drained by the kernel idle context. Executing
    // shell commands directly from an IRQ can accidentally make them children
    // of whichever background user process the interrupt preempted.
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
    interrupt_controller_eoi(0);
    driver_irq_dispatch(0);
    scheduler_wake_sleeping_processes(pit.get_tick());
    scheduler_on_tick();
    uint64_t result = 0;
    if (scheduler_should_preempt_current()) {
        result = TIMER_PREEMPT_TO_KERNEL;
    }
    Thread* thread = current_thread();
    if (thread != 0 && thread->context->timeslice_ticks == 0) {
        thread->context->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    }
    return result;
}

extern "C" void user_test_interrupt_handler64() {
    user_test_count++;
    print("\nUser mode reached.");
}

extern "C" void user_exit_interrupt_handler64() {
    print("\nUser mode exit requested.");
}
