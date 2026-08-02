extern "C" void keyboard_handler64() {
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) local->interrupt_depth++;
    if (!interrupt_controller_claim_external_irq(1)) {
        interrupt_controller_eoi(1);
        if (cpu_local_validate(local) && local->interrupt_depth != 0) {
            local->interrupt_depth--;
        }
        return;
    }
    if (kernel_in_tlb_wait()) {
        keyboard.defer_interrupt();
        interrupt_controller_eoi(1);
        if (cpu_local_validate(local) && local->interrupt_depth != 0) {
            local->interrupt_depth--;
        }
        return;
    }
    interrupt_controller_eoi(1);
    keyboard.handle();
    driver_irq_dispatch(1);
    if (cpu_local_validate(local) && local->interrupt_depth != 0) {
        local->interrupt_depth--;
    }
}

extern "C" void mouse_handler64() {
    mouse.handle();
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
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        local->interrupt_depth++;
        local->timer_interrupt_count++;
    }
    if (!interrupt_controller_claim_external_irq(0)) {
        interrupt_controller_eoi(0);
        if (cpu_local_validate(local) && local->interrupt_depth != 0) {
            local->interrupt_depth--;
        }
        return 0;
    }
    if (kernel_in_tlb_wait()) {
        interrupt_controller_eoi(0);
        if (cpu_local_validate(local) && local->interrupt_depth != 0) {
            local->interrupt_depth--;
        }
        return 0;
    }
    pit.handle();
    interrupt_controller_eoi(0);
    driver_irq_dispatch(0);
    scheduler_wake_sleeping_processes(pit.get_tick());
    if (!cpu_local_validate(local) || !local->scheduler_enabled) {
        scheduler_on_tick();
    }
    uint64_t result = 0;
    if (!cpu_local_validate(local) || !local->scheduler_enabled) {
        if (scheduler_should_preempt_current()) {
            scheduler_note_preemption(current_thread());
            result = TIMER_PREEMPT_TO_KERNEL;
        }
        Thread* thread = current_thread();
        if (thread != 0 && thread->context->timeslice_ticks == 0) {
            scheduler_refresh_timeslice(thread);
        }
    }
    if (cpu_local_validate(local) && local->interrupt_depth != 0) {
        local->interrupt_depth--;
    }
    return result;
}

extern "C" uint64_t local_timer_handler64() {
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        local->interrupt_depth++;
        local->local_timer_interrupt_count++;
        local->scheduler_tick_count++;
    }
    if (kernel_in_tlb_wait()) {
        interrupt_controller_eoi(0);
        if (cpu_local_validate(local) && local->interrupt_depth != 0) {
            local->interrupt_depth--;
        }
        return 0;
    }
    interrupt_controller_eoi(0);
    scheduler_on_tick();
    uint64_t result = 0;
    Thread* thread = current_thread();
    const int quantum_expired =
        thread != 0 && thread->context != 0 &&
        thread->context->timeslice_ticks == 0;
    if (quantum_expired) {
        scheduler_note_preemption(thread);
    }
    if (quantum_expired && scheduler_should_preempt_current()) {
        result = TIMER_PREEMPT_TO_KERNEL;
    }
    if (thread != 0 && thread->context->timeslice_ticks == 0) {
        scheduler_refresh_timeslice(thread);
    }
    if (cpu_local_validate(local) && local->interrupt_depth != 0) {
        local->interrupt_depth--;
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
