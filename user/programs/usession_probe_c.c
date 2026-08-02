#include <os64/os64.h>

#define SESSION_TEST_TIMEOUT 400u

static void copy_name(char* out, const char* name) {
    uint32_t i = 0;
    while (name[i] != '\0' && i + 1u < OS_SERVICE_NAME_MAX) {
        out[i] = name[i];
        i++;
    }
    out[i] = '\0';
}

static long ensure_manager(OsProcessIdentity* manager) {
    if (os_service_find_owner_identity(OS_SERVICE_MANAGER_NAME, manager) ==
        OS_SUCCESS) return OS_SUCCESS;
    long pid = os_run("serviced_c.elf");
    if (pid < 0) return pid;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < SESSION_TEST_TIMEOUT) {
        if (os_service_find_owner_identity(OS_SERVICE_MANAGER_NAME, manager) ==
            OS_SUCCESS) {
            os_set_background(manager->pid, 1);
            return OS_SUCCESS;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static void wait_elapsed(uint32_t ticks) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < ticks) {
        /* A background process may have its sleep interrupted by lifecycle
         * notifications.  Elapsed time, rather than one sleep result, is the
         * synchronization contract used by this integration probe. */
        os_sleep(1);
        os_yield();
    }
}

static long manager_request(OsProcessIdentity manager, uint32_t command,
                            const char* name, OsServiceManagerReply* out) {
    OsServiceManagerRequest request;
    os_memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.command = command;
    request.request_id = os_msg_next_request_id();
    copy_name(request.name, name);
    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    long result = os_msg_send_to_identity(manager, &message);
    if (result < 0) return result;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < SESSION_TEST_TIMEOUT) {
        OsIpcMessage reply_message;
        result = os_msg_wait_timeout(&reply_message, 10);
        if (result == OS_ERR_TIMEOUT || result == OS_ERR_CANCELLED ||
            result == OS_ERR_WOULD_BLOCK || result == OS_ERR_NOT_READY)
            continue;
        if (result < 0) return result;
        if (reply_message.type != OS_IPC_MESSAGE_REPLY ||
            reply_message.length != sizeof(*out) ||
            reply_message.sender_pid != manager.pid) continue;
        os_memcpy(out, reply_message.payload, sizeof(*out));
        if (out->size != sizeof(*out) ||
            out->request_id != request.request_id ||
            out->command != command) continue;
        return out->result;
    }
    return OS_ERR_TIMEOUT;
}

static long wait_session_pid(OsProcessIdentity manager, uint32_t previous,
                             uint32_t* current) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < SESSION_TEST_TIMEOUT) {
        OsServiceManagerReply status;
        long result = manager_request(manager, OS_SERVICE_MANAGER_CMD_STATUS,
                                      "session", &status);
        if (result == OS_SUCCESS && status.pid != 0 &&
            status.state == OS_SERVICE_MANAGER_STATE_RUNNING &&
            status.pid != previous) {
            *current = status.pid;
            return OS_SUCCESS;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static long verify_privileged_layer_denial(void) {
    const uint32_t flags[3] = {
        OS_WINDOW_FLAG_LAYER_DESKTOP,
        OS_WINDOW_FLAG_LAYER_PANEL,
        OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY,
    };
    for (uint32_t i = 0; i < 3; i++) {
        OsWindow window;
        long result = os_window_create_layer(&window, 0, 0, 32, 32,
                                             flags[i]);
        if (result == OS_SUCCESS) {
            os_window_destroy(&window);
            return OS_ERR_PERMISSION_DENIED;
        }
        if (result != OS_ERR_PERMISSION_DENIED) return result;
    }
    os_puts("[ulayer] privileged layers denied");
    return OS_SUCCESS;
}

int main(void) {
    OsProcessIdentity manager;
    if (ensure_manager(&manager) < 0) {
        os_puts("[session-test] manager unavailable");
        return 1;
    }
    OsServiceManagerReply reply;
    long result = manager_request(manager, OS_SERVICE_MANAGER_CMD_START,
                                  "session", &reply);
    if (result < 0) {
        os_printf("[session-test] start failed %ld\n", result);
        return 1;
    }
    uint32_t first = reply.pid;
    if (first == 0) {
        os_puts("[session-test] first identity missing");
        return 1;
    }
    /* The first full-screen surface faults in and paints several MiB at O0.
     * Keep the probe off its critical path before injecting teardown. */
    wait_elapsed(120);
    result = verify_privileged_layer_denial();
    if (result < 0) {
        os_printf("[session-test] layer probe failed %ld\n", result);
        return 1;
    }
    result = manager_request(manager, OS_SERVICE_MANAGER_CMD_CRASH,
                             "session", &reply);
    /* Killing the GUI owner cancels outstanding waits during the ownership
     * handoff.  A lost synchronous reply is therefore not a failed crash;
     * the subsequent identity change is the authoritative assertion. */
    if (result < 0 && result != OS_ERR_TIMEOUT &&
        result != OS_ERR_CANCELLED) {
        os_printf("[session-test] crash injection failed %ld\n", result);
        return 1;
    }
    os_printf("[session-test] injected crash session pid=%u\n", first);
    wait_elapsed(30);
    uint32_t second = 0;
    if (wait_session_pid(manager, first, &second) < 0) {
        os_puts("[session-test] restart identity missing");
        return 1;
    }
    wait_elapsed(20);
    result = manager_request(manager, OS_SERVICE_MANAGER_CMD_HEALTH,
                             "session", &reply);
    if (result < 0 ||
        reply.health != OS_SERVICE_HEALTH_HEALTHY) {
        os_printf("[session-test] restarted session unhealthy result=%ld health=%u\n",
                  result, reply.health);
        return 1;
    }
    result = manager_request(manager, OS_SERVICE_MANAGER_CMD_STOP,
                             "session", &reply);
    if (result < 0) {
        os_printf("[session-test] stop failed %ld\n", result);
        return 1;
    }
    wait_elapsed(20);
    os_printf("[session-test] PASS first=%u second=%u\n", first, second);
    os_reap_children();
    return 0;
}
