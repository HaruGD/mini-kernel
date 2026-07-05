#include <os64/os64.h>

typedef struct ManagedService {
    const char* name;
    const char* program;
    const char* dependency;
    uint32_t pid;
    uint32_t running;
} ManagedService;

#define MANAGED_SERVICE_COUNT 4u

static ManagedService services[MANAGED_SERVICE_COUNT] = {
    {"base", "svc_base_c.elf", 0, 0, 0},
    {"demo", "svc_demo_c.elf", "base", 0, 0},
    {"input", "inputd_c.elf", 0, 0, 0},
    {"display", "displayd_c.elf", 0, 0, 0},
};

static void copy_name(char* out, const char* name) {
    uint32_t i = 0;
    if (out == 0) {
        return;
    }
    while (name != 0 && name[i] != '\0' && i + 1 < OS_SERVICE_NAME_MAX) {
        out[i] = name[i];
        i++;
    }
    out[i] = '\0';
}

static ManagedService* find_managed(ManagedService* services, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++) {
        if (os_streq(services[i].name, name)) {
            return &services[i];
        }
    }
    return 0;
}

static int service_registry_running(ManagedService* service, OsServiceInfo* info) {
    if (service == 0) {
        return 0;
    }
    if (os_service_find(service->name, info) == OS_SUCCESS) {
        service->pid = info->owner_pid;
        service->running = 1;
        return 1;
    }
    service->pid = 0;
    service->running = 0;
    return 0;
}

static int start_service(ManagedService* services, uint32_t count, ManagedService* service) {
    OsServiceInfo info;
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    if (service_registry_running(service, &info)) {
        return OS_SUCCESS;
    }
    if (service->dependency != 0) {
        ManagedService* dependency = find_managed(services, count, service->dependency);
        int dep_result = start_service(services, count, dependency);
        if (dep_result < 0) {
            return dep_result;
        }
    }
    if (os_run(service->program) < 0) {
        return OS_ERR_IO;
    }
    if (!service_registry_running(service, &info)) {
        return OS_ERR_NOT_READY;
    }
    os_set_background(service->pid, 1);
    os_printf("[serviced] started %s pid=%u\n", service->name, service->pid);
    return OS_SUCCESS;
}

static int stop_service(ManagedService* service) {
    OsServiceInfo info;
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    if (!service_registry_running(service, &info)) {
        return OS_SUCCESS;
    }
    if (os_kill(service->pid) < 0) {
        return OS_ERR_IO;
    }
    os_reap_children();
    service->pid = 0;
    service->running = 0;
    os_printf("[serviced] stopped %s\n", service->name);
    return OS_SUCCESS;
}

static int restart_service(ManagedService* services, uint32_t count, ManagedService* service) {
    int result = stop_service(service);
    if (result < 0) {
        return result;
    }
    return start_service(services, count, service);
}

static uint32_t service_state(ManagedService* service, uint32_t* pid_out, uint32_t* gen_out) {
    OsServiceInfo info;
    if (pid_out != 0) {
        *pid_out = 0;
    }
    if (gen_out != 0) {
        *gen_out = 0;
    }
    if (service != 0 && service_registry_running(service, &info)) {
        if (pid_out != 0) {
            *pid_out = info.owner_pid;
        }
        if (gen_out != 0) {
            *gen_out = info.generation;
        }
        return OS_SERVICE_MANAGER_STATE_RUNNING;
    }
    return service == 0 ? OS_SERVICE_MANAGER_STATE_UNKNOWN : OS_SERVICE_MANAGER_STATE_STOPPED;
}

static void send_reply(const OsIpcMessage* request_message,
                       const OsServiceManagerRequest* request,
                       int result,
                       uint32_t pid,
                       uint32_t state,
                       uint32_t generation) {
    OsServiceManagerReply reply;
    reply.size = sizeof(reply);
    reply.command = request->command;
    reply.result = result;
    reply.pid = pid;
    reply.state = state;
    reply.generation = generation;
    copy_name(reply.name, request->name);

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    long send_result = os_msg_send(request_message->sender_pid, &message);
    if (send_result < 0) {
        os_printf("[serviced] reply failed pid=%u result=%ld\n",
                  request_message->sender_pid,
                  send_result);
    }
}

static int handle_request(ManagedService* services, uint32_t count, const OsIpcMessage* message) {
    OsServiceManagerRequest request;
    if (message->length != sizeof(request)) {
        return 1;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request)) {
        return 1;
    }

    int result = OS_SUCCESS;
    uint32_t pid = 0;
    uint32_t state = OS_SERVICE_MANAGER_STATE_UNKNOWN;
    uint32_t generation = 0;
    ManagedService* service = find_managed(services, count, request.name);

    if (request.command == OS_SERVICE_MANAGER_CMD_PING) {
        copy_name(request.name, OS_SERVICE_MANAGER_NAME);
        state = OS_SERVICE_MANAGER_STATE_RUNNING;
        pid = (uint32_t)os_getpid();
    } else if (request.command == OS_SERVICE_MANAGER_CMD_START) {
        result = start_service(services, count, service);
        state = service_state(service, &pid, &generation);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_STOP) {
        result = stop_service(service);
        state = service_state(service, &pid, &generation);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_RESTART) {
        result = restart_service(services, count, service);
        state = service_state(service, &pid, &generation);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_STATUS) {
        state = service_state(service, &pid, &generation);
        if (state == OS_SERVICE_MANAGER_STATE_UNKNOWN) {
            result = OS_ERR_NOT_FOUND;
        }
    } else if (request.command == OS_SERVICE_MANAGER_CMD_EXIT) {
        for (uint32_t i = count; i > 0; i--) {
            stop_service(&services[i - 1]);
        }
        state = OS_SERVICE_MANAGER_STATE_STOPPED;
    } else {
        result = OS_ERR_INVALID_ARGUMENT;
    }

    int should_exit = request.command == OS_SERVICE_MANAGER_CMD_EXIT && result == OS_SUCCESS;
    send_reply(message, &request, result, pid, state, generation);
    if (should_exit) {
        os_yield();
    }
    return should_exit;
}

int main(void) {
    long result = os_service_register(OS_SERVICE_MANAGER_NAME, OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[serviced] register failed %ld\n", result);
        return 1;
    }

    os_printf("[serviced] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        os_sleep(100000u);
        OsIpcMessage message;
        result = os_msg_wait(&message);
        if (result < 0) {
            os_printf("[serviced] wait failed %ld\n", result);
            return 1;
        }
        if (message.type == OS_IPC_MESSAGE_REQUEST &&
            handle_request(services, MANAGED_SERVICE_COUNT, &message)) {
            break;
        }
    }

    os_service_unregister(OS_SERVICE_MANAGER_NAME);
    os_puts("[serviced] exit");
    return 0;
}
