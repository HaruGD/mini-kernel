#include <os64/os64.h>

#define RECOVERY_TIMEOUT_TICKS 1000u

static void copy_name(char* out, const char* name) {
    uint32_t i = 0;
    while (name[i] != '\0' && i + 1u < OS_SERVICE_NAME_MAX) {
        out[i] = name[i];
        i++;
    }
    out[i] = '\0';
}

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

static long crash_service(const char* name) {
    OsProcessIdentity manager;
    long result = os_service_find_owner_identity(OS_SERVICE_MANAGER_NAME,
                                                 &manager);
    if (result < 0) {
        return result;
    }
    OsServiceManagerRequest request;
    os_memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.command = OS_SERVICE_MANAGER_CMD_CRASH;
    request.request_id = os_msg_next_request_id();
    copy_name(request.name, name);

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    result = os_msg_send_to_identity(manager, &message);
    if (result < 0) {
        return result;
    }
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < RECOVERY_TIMEOUT_TICKS) {
        result = os_msg_wait_timeout(&message, 20);
        if (result == OS_ERR_TIMEOUT || result == OS_ERR_WOULD_BLOCK) {
            continue;
        }
        if (result < 0) {
            return result;
        }
        if (message.sender_pid != manager.pid ||
            message.sender_generation != manager.generation ||
            message.type != OS_IPC_MESSAGE_REPLY ||
            message.length != sizeof(OsServiceManagerReply)) {
            continue;
        }
        OsServiceManagerReply reply;
        os_memcpy(&reply, message.payload, sizeof(reply));
        if (reply.size != sizeof(reply) ||
            reply.command != OS_SERVICE_MANAGER_CMD_CRASH ||
            reply.request_id != request.request_id) {
            continue;
        }
        return reply.result;
    }
    return OS_ERR_TIMEOUT;
}

static long wait_session_state(uint32_t state,
                               uint32_t generation_not_equal,
                               OsDisplaySessionInfo* result_info) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < RECOVERY_TIMEOUT_TICKS) {
        OsDisplaySessionInfo info;
        long result = os_display_session_get_info(&info);
        if (result == OS_SUCCESS && info.state == state &&
            (generation_not_equal == 0 ||
             info.generation != generation_not_equal)) {
            if (result_info != 0) {
                *result_info = info;
            }
            return OS_SUCCESS;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static long wait_new_service(const char* name,
                             OsProcessIdentity previous,
                             OsProcessIdentity* current) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < RECOVERY_TIMEOUT_TICKS) {
        OsProcessIdentity candidate;
        if (os_service_find_owner_identity(name, &candidate) == OS_SUCCESS &&
            !identity_equal(candidate, previous)) {
            if (current != 0) {
                *current = candidate;
            }
            return OS_SUCCESS;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static long paint(OsWindow* window, uint32_t color) {
    OsSurfaceCanvas canvas;
    long result = os_surface_canvas_init(&canvas, window->pixels,
                                         &window->surface_info);
    if (result < 0) {
        return result;
    }
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){0, 0, (int32_t)canvas.width, (int32_t)canvas.height},
        color);
    if (result < 0) {
        return result;
    }
    result = os_surface_canvas_draw_text(&canvas, 20, 24, "RECOVERY TEST",
                                         OS_RGB(245, 248, 255), color,
                                         OS_SURFACE_TEXT_TRANSPARENT_BG);
    return result < 0 ? result : os_window_damage_all(window);
}

int main(void) {
    OsWindow window;
    if (os_window_create(&window, 96, 82, 300, 190) < 0 ||
        paint(&window, OS_RGB(31, 78, 121)) < 0) {
        os_puts("[ugui-recovery] initial window failed");
        return 1;
    }
    OsDisplaySessionInfo initial;
    if (wait_session_state(OS_DISPLAY_SESSION_GUI_ACTIVE, 0, &initial) < 0) {
        os_puts("[ugui-recovery] initial session failed");
        os_window_abandon(&window);
        return 1;
    }
    os_printf("[ugui-recovery] initial generation=%u\n", initial.generation);

    OsProcessIdentity old_display;
    if (os_service_find_owner_identity("display", &old_display) < 0 ||
        crash_service("display") < 0 ||
        wait_session_state(OS_DISPLAY_SESSION_CONSOLE_ACTIVE, 0, 0) < 0) {
        os_puts("[ugui-recovery] display crash fallback failed");
        os_window_abandon(&window);
        return 1;
    }
    os_printf("[ugui-recovery] display crash restored console generation=%u\n",
              initial.generation);
    if (wait_new_service("display", old_display, 0) < 0) {
        os_puts("[ugui-recovery] display restart failed");
        os_window_abandon(&window);
        return 1;
    }
    OsDisplaySessionInfo display_recovered;
    if (wait_session_state(OS_DISPLAY_SESSION_GUI_ACTIVE, initial.generation,
                           &display_recovered) < 0 ||
        paint(&window, OS_RGB(42, 126, 92)) < 0) {
        os_puts("[ugui-recovery] display reconnect failed");
        os_window_abandon(&window);
        return 1;
    }
    os_printf("[ugui-recovery] display recovered generation=%u\n",
              display_recovered.generation);

    OsProcessIdentity old_window;
    if (os_service_find_owner_identity("window", &old_window) < 0 ||
        crash_service("window") < 0 ||
        wait_session_state(OS_DISPLAY_SESSION_CONSOLE_ACTIVE, 0, 0) < 0) {
        os_puts("[ugui-recovery] window crash fallback failed");
        os_window_abandon(&window);
        return 1;
    }
    os_printf("[ugui-recovery] window crash restored console generation=%u\n",
              display_recovered.generation);
    os_window_abandon(&window);
    if (wait_new_service("window", old_window, 0) < 0 ||
        os_window_create(&window, 140, 118, 280, 176) < 0 ||
        paint(&window, OS_RGB(116, 61, 133)) < 0) {
        os_puts("[ugui-recovery] window reconnect failed");
        os_window_abandon(&window);
        return 1;
    }
    OsDisplaySessionInfo window_recovered;
    if (wait_session_state(OS_DISPLAY_SESSION_GUI_ACTIVE,
                           display_recovered.generation,
                           &window_recovered) < 0) {
        os_puts("[ugui-recovery] replacement session failed");
        os_window_abandon(&window);
        return 1;
    }
    os_printf("[ugui-recovery] window recovered generation=%u\n",
              window_recovered.generation);
    if (os_window_destroy(&window) < 0 ||
        wait_session_state(OS_DISPLAY_SESSION_CONSOLE_ACTIVE, 0, 0) < 0) {
        os_puts("[ugui-recovery] final console restore failed");
        os_window_abandon(&window);
        return 1;
    }
    os_puts("[ugui-recovery] lifecycle OK");
    return 0;
}
