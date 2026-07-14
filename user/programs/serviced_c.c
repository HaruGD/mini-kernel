#include <os64/os64.h>

#define SERVICE_START_TIMEOUT_TICKS 50u
#define SERVICE_STOP_TIMEOUT_TICKS 50u
#define SERVICE_HEALTH_TIMEOUT_TICKS 25u
#define SERVICE_SUPERVISOR_INTERVAL_TICKS 10u
#define SERVICE_RESTART_BACKOFF_TICKS 20u
#define SERVICE_RESTART_LIMIT 3u

typedef struct ManagedService {
    const char* name;
    const char* program;
    const char* dependency;
    uint32_t permissions;
    uint32_t restart_policy;
    uint32_t pid;
    uint32_t owner_generation;
    uint32_t state;
    uint32_t state_generation;
    uint32_t health;
    uint32_t last_failure;
    uint32_t restart_count;
    uint32_t restart_at;
} ManagedService;

#define SERVICE_BASE_PERMISSIONS \
    (OS_PROCESS_PERMISSION_SERVICE_REGISTER | OS_PROCESS_PERMISSION_IPC)

#define MANAGED_SERVICE_COUNT 6u

static ManagedService services[MANAGED_SERVICE_COUNT] = {
    {"base", "svc_base_c.elf", 0, SERVICE_BASE_PERMISSIONS,
     OS_SERVICE_RESTART_DISABLED, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
    {"demo", "svc_demo_c.elf", "base", SERVICE_BASE_PERMISSIONS,
     OS_SERVICE_RESTART_DISABLED, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
    {"input", "inputd_c.elf", 0,
     SERVICE_BASE_PERMISSIONS | OS_PROCESS_PERMISSION_INPUT,
     OS_SERVICE_RESTART_ON_FAILURE, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
    {"display", "displayd_c.elf", 0,
     SERVICE_BASE_PERMISSIONS | OS_PROCESS_PERMISSION_DISPLAY |
         OS_PROCESS_PERMISSION_SHARED_SURFACE,
     OS_SERVICE_RESTART_ON_FAILURE, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
    {"restricted", "svc_demo_c.elf restricted", 0, SERVICE_BASE_PERMISSIONS,
     OS_SERVICE_RESTART_DISABLED, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
    {"crash", "svc_demo_c.elf crash", 0, SERVICE_BASE_PERMISSIONS,
     OS_SERVICE_RESTART_ON_FAILURE, 0, 0, OS_SERVICE_MANAGER_STATE_STOPPED, 0,
     OS_SERVICE_HEALTH_UNKNOWN, OS_SERVICE_FAILURE_NONE, 0, 0},
};

static uint32_t next_state_generation = 1u;

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

static int tick_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static ManagedService* find_managed(const char* name) {
    for (uint32_t i = 0; i < MANAGED_SERVICE_COUNT; i++) {
        if (os_streq(services[i].name, name)) {
            return &services[i];
        }
    }
    return 0;
}

static int managed_index(const ManagedService* service) {
    if (service == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < MANAGED_SERVICE_COUNT; i++) {
        if (&services[i] == service) {
            return (int)i;
        }
    }
    return -1;
}

static void transition_service(ManagedService* service, uint32_t state) {
    if (service == 0 || service->state == state) {
        return;
    }
    service->state = state;
    service->state_generation = next_state_generation++;
    if (next_state_generation == 0) {
        next_state_generation = 1;
    }
}

static void fail_service(ManagedService* service, uint32_t failure) {
    if (service == 0) {
        return;
    }
    service->last_failure = failure;
    service->health = failure == OS_SERVICE_FAILURE_HEALTH_TIMEOUT
        ? OS_SERVICE_HEALTH_UNRESPONSIVE
        : OS_SERVICE_HEALTH_UNKNOWN;
    transition_service(service, OS_SERVICE_MANAGER_STATE_FAILED);
    if (service->restart_policy == OS_SERVICE_RESTART_ON_FAILURE &&
        service->restart_count < SERVICE_RESTART_LIMIT) {
        service->restart_at = (uint32_t)os_time_ticks() + SERVICE_RESTART_BACKOFF_TICKS;
    }
}

static int registry_lookup(ManagedService* service, OsServiceInfo* info) {
    if (service == 0) {
        return 0;
    }
    if (os_service_find(service->name, info) != OS_SUCCESS) {
        return 0;
    }
    service->pid = info->owner_pid;
    OsProcessIdentity identity;
    if (os_service_find_owner_identity(service->name, &identity) != OS_SUCCESS ||
        identity.pid != info->owner_pid) {
        return 0;
    }
    service->owner_generation = identity.generation;
    return 1;
}

static int wait_for_registry(ManagedService* service, int present, uint32_t timeout_ticks) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < timeout_ticks) {
        OsServiceInfo info;
        int found = registry_lookup(service, &info);
        if (found == present) {
            return 1;
        }
        os_sleep(1);
    }
    return 0;
}

static int validate_dependency_node(uint32_t index, uint8_t* visiting, uint8_t* visited) {
    if (visited[index]) {
        return OS_SUCCESS;
    }
    if (visiting[index]) {
        os_printf("[serviced] dependency cycle at %s\n", services[index].name);
        return OS_ERR_INVALID_ARGUMENT;
    }
    visiting[index] = 1;
    if (services[index].dependency != 0) {
        ManagedService* dependency = find_managed(services[index].dependency);
        int dependency_index = managed_index(dependency);
        if (dependency_index < 0) {
            os_printf("[serviced] missing dependency %s -> %s\n",
                      services[index].name,
                      services[index].dependency);
            return OS_ERR_NOT_FOUND;
        }
        int result = validate_dependency_node((uint32_t)dependency_index, visiting, visited);
        if (result < 0) {
            return result;
        }
    }
    visiting[index] = 0;
    visited[index] = 1;
    return OS_SUCCESS;
}

static int validate_dependencies(void) {
    uint8_t visiting[MANAGED_SERVICE_COUNT];
    uint8_t visited[MANAGED_SERVICE_COUNT];
    os_memset(visiting, 0, sizeof(visiting));
    os_memset(visited, 0, sizeof(visited));
    for (uint32_t i = 0; i < MANAGED_SERVICE_COUNT; i++) {
        int result = validate_dependency_node(i, visiting, visited);
        if (result < 0) {
            return result;
        }
    }
    return OS_SUCCESS;
}

static int start_service_checked(ManagedService* service, uint8_t* starting, int automatic) {
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    int index = managed_index(service);
    if (index < 0 || starting[index]) {
        fail_service(service, OS_SERVICE_FAILURE_DEPENDENCY);
        return OS_ERR_INVALID_ARGUMENT;
    }

    OsServiceInfo info;
    if (registry_lookup(service, &info)) {
        transition_service(service, OS_SERVICE_MANAGER_STATE_RUNNING);
        service->health = OS_SERVICE_HEALTH_HEALTHY;
        return OS_SUCCESS;
    }

    starting[index] = 1;
    if (service->dependency != 0) {
        ManagedService* dependency = find_managed(service->dependency);
        int dependency_result = start_service_checked(dependency, starting, automatic);
        if (dependency_result < 0) {
            starting[index] = 0;
            fail_service(service, OS_SERVICE_FAILURE_DEPENDENCY);
            return dependency_result;
        }
    }

    transition_service(service, OS_SERVICE_MANAGER_STATE_STARTING);
    service->health = OS_SERVICE_HEALTH_UNKNOWN;
    long result = os_run_with_permissions(service->program, service->permissions);
    if (result < 0) {
        starting[index] = 0;
        fail_service(service, OS_SERVICE_FAILURE_LAUNCH);
        return (int)result;
    }
    if (!wait_for_registry(service, 1, SERVICE_START_TIMEOUT_TICKS)) {
        starting[index] = 0;
        fail_service(service, OS_SERVICE_FAILURE_START_TIMEOUT);
        return OS_ERR_TIMEOUT;
    }

    os_set_background(service->pid, 1);
    service->last_failure = OS_SERVICE_FAILURE_NONE;
    service->health = OS_SERVICE_HEALTH_HEALTHY;
    service->restart_at = 0;
    if (!automatic) {
        service->restart_count = 0;
    }
    transition_service(service, OS_SERVICE_MANAGER_STATE_RUNNING);
    starting[index] = 0;
    os_printf("[serviced] started %s pid=%u state_gen=%u perms=0x%x\n",
              service->name,
              service->pid,
              service->state_generation,
              service->permissions);
    return OS_SUCCESS;
}

static int start_service(ManagedService* service, int automatic) {
    uint8_t starting[MANAGED_SERVICE_COUNT];
    os_memset(starting, 0, sizeof(starting));
    return start_service_checked(service, starting, automatic);
}

static int has_running_dependent(const ManagedService* service) {
    for (uint32_t i = 0; i < MANAGED_SERVICE_COUNT; i++) {
        if (services[i].dependency != 0 && os_streq(services[i].dependency, service->name) &&
            (services[i].state == OS_SERVICE_MANAGER_STATE_RUNNING ||
             services[i].state == OS_SERVICE_MANAGER_STATE_STARTING)) {
            return 1;
        }
    }
    return 0;
}

static int stop_service(ManagedService* service, int explicit_stop) {
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    if (explicit_stop && has_running_dependent(service)) {
        return OS_ERR_NOT_READY;
    }

    OsServiceInfo info;
    if (!registry_lookup(service, &info)) {
        service->pid = 0;
        service->owner_generation = 0;
        service->health = OS_SERVICE_HEALTH_UNKNOWN;
        if (explicit_stop) {
            service->last_failure = OS_SERVICE_FAILURE_NONE;
            service->restart_count = 0;
        }
        transition_service(service, OS_SERVICE_MANAGER_STATE_STOPPED);
        return OS_SUCCESS;
    }

    transition_service(service, OS_SERVICE_MANAGER_STATE_STOPPING);
    if (os_kill(service->pid) < 0 ||
        !wait_for_registry(service, 0, SERVICE_STOP_TIMEOUT_TICKS)) {
        fail_service(service, OS_SERVICE_FAILURE_STOP_TIMEOUT);
        return OS_ERR_TIMEOUT;
    }
    os_reap_children();
    service->pid = 0;
    service->owner_generation = 0;
    service->health = OS_SERVICE_HEALTH_UNKNOWN;
    service->last_failure = OS_SERVICE_FAILURE_NONE;
    service->restart_at = 0;
    if (explicit_stop) {
        service->restart_count = 0;
    }
    transition_service(service, OS_SERVICE_MANAGER_STATE_STOPPED);
    os_printf("[serviced] stopped %s state_gen=%u\n",
              service->name,
              service->state_generation);
    return OS_SUCCESS;
}

static int restart_service(ManagedService* service) {
    int result = stop_service(service, 0);
    if (result < 0) {
        return result;
    }
    service->restart_count = 0;
    return start_service(service, 0);
}

static int crash_service(ManagedService* service) {
    OsServiceInfo info;
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    if (!registry_lookup(service, &info)) {
        return OS_ERR_NOT_READY;
    }
    if (os_kill(service->pid) < 0 ||
        !wait_for_registry(service, 0, SERVICE_STOP_TIMEOUT_TICKS)) {
        return OS_ERR_TIMEOUT;
    }
    os_reap_children();
    os_printf("[serviced] injected crash %s pid=%u\n", service->name, service->pid);
    service->pid = 0;
    service->owner_generation = 0;
    fail_service(service, OS_SERVICE_FAILURE_EXITED);
    return OS_SUCCESS;
}

static int health_service(ManagedService* service) {
    OsServiceInfo info;
    if (service == 0) {
        return OS_ERR_NOT_FOUND;
    }
    if (!registry_lookup(service, &info)) {
        fail_service(service, OS_SERVICE_FAILURE_EXITED);
        return OS_ERR_NOT_READY;
    }

    OsServiceQueryRequest query;
    query.size = sizeof(query);
    query.command = OS_SERVICE_QUERY_HEALTH;
    query.flags = 0;
    query.request_id = os_msg_next_request_id();

    OsIpcMessage request;
    os_msg_init(&request, OS_IPC_MESSAGE_REQUEST);
    request.flags = OS_IPC_FLAG_REQUEST_REPLY;
    request.length = sizeof(query);
    os_memcpy(request.payload, &query, sizeof(query));
    if (os_msg_send(info.owner_pid, &request) < 0) {
        fail_service(service, OS_SERVICE_FAILURE_HEALTH_TIMEOUT);
        return OS_ERR_NOT_READY;
    }

    OsIpcMessage response;
    long result = os_msg_wait_timeout(&response, SERVICE_HEALTH_TIMEOUT_TICKS);
    if (result < 0 || response.sender_pid != info.owner_pid ||
        response.type != OS_IPC_MESSAGE_REPLY ||
        response.length != sizeof(OsServiceHealthReply)) {
        fail_service(service, OS_SERVICE_FAILURE_HEALTH_TIMEOUT);
        return OS_ERR_TIMEOUT;
    }
    OsServiceHealthReply reply;
    os_memcpy(&reply, response.payload, sizeof(reply));
    if (reply.size != sizeof(reply) || reply.command != OS_SERVICE_QUERY_HEALTH ||
        reply.request_id != query.request_id || reply.result < 0 || reply.ready == 0) {
        fail_service(service, OS_SERVICE_FAILURE_HEALTH_TIMEOUT);
        return OS_ERR_NOT_READY;
    }
    service->health = OS_SERVICE_HEALTH_HEALTHY;
    return OS_SUCCESS;
}

static void supervise_services(void) {
    uint32_t now = (uint32_t)os_time_ticks();
    for (uint32_t i = 0; i < MANAGED_SERVICE_COUNT; i++) {
        ManagedService* service = &services[i];
        if (service->state == OS_SERVICE_MANAGER_STATE_RUNNING) {
            OsServiceInfo info;
            if (!registry_lookup(service, &info)) {
                fail_service(service, OS_SERVICE_FAILURE_EXITED);
            }
        }
        if (service->state != OS_SERVICE_MANAGER_STATE_FAILED ||
            service->restart_policy != OS_SERVICE_RESTART_ON_FAILURE ||
            service->restart_at == 0 || !tick_reached(now, service->restart_at)) {
            continue;
        }
        if (service->restart_count >= SERVICE_RESTART_LIMIT) {
            service->restart_at = 0;
            service->last_failure = OS_SERVICE_FAILURE_RESTART_LIMIT;
            continue;
        }
        service->restart_count++;
        service->restart_at = 0;
        os_printf("[serviced] auto-restart %s attempt=%u\n",
                  service->name,
                  service->restart_count);
        if (start_service(service, 1) < 0 &&
            service->restart_count >= SERVICE_RESTART_LIMIT) {
            service->last_failure = OS_SERVICE_FAILURE_RESTART_LIMIT;
            service->restart_at = 0;
        }
    }
}

static void send_reply(const OsIpcMessage* request_message,
                       const OsServiceManagerRequest* request,
                       int result,
                       const ManagedService* service) {
    OsServiceManagerReply reply;
    os_memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.command = request->command;
    reply.result = result;
    reply.request_id = request->request_id;
    reply.state = OS_SERVICE_MANAGER_STATE_UNKNOWN;
    copy_name(reply.name, request->name);
    if (service != 0) {
        reply.pid = service->pid;
        reply.state = service->state;
        reply.generation = service->state_generation;
        reply.last_failure = service->last_failure;
        reply.restart_policy = service->restart_policy;
        reply.restart_count = service->restart_count;
        reply.health = service->health;
        reply.permissions = service->permissions;
    } else if (request->command == OS_SERVICE_MANAGER_CMD_PING) {
        reply.pid = (uint32_t)os_getpid();
        reply.state = OS_SERVICE_MANAGER_STATE_RUNNING;
        reply.generation = 1;
        reply.health = OS_SERVICE_HEALTH_HEALTHY;
        reply.permissions = OS_PROCESS_PERMISSION_ALL;
    } else if (request->command == OS_SERVICE_MANAGER_CMD_EXIT) {
        reply.state = OS_SERVICE_MANAGER_STATE_STOPPED;
    }

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    OsProcessIdentity target = os_msg_sender_identity(request_message);
    if (os_msg_send_to_identity(target, &message) < 0) {
        os_printf("[serviced] reply failed pid=%u gen=%u\n", target.pid, target.generation);
    }
}

static int handle_request(const OsIpcMessage* message) {
    OsServiceManagerRequest request;
    if (message->length != sizeof(request)) {
        return 0;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request)) {
        return 0;
    }

    int result = OS_SUCCESS;
    ManagedService* service = find_managed(request.name);
    if (request.command == OS_SERVICE_MANAGER_CMD_PING) {
        copy_name(request.name, OS_SERVICE_MANAGER_NAME);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_START) {
        if (service != 0) {
            service->restart_count = 0;
        }
        result = start_service(service, 0);
        if (result < 0 && service != 0 &&
            service->restart_policy == OS_SERVICE_RESTART_ON_FAILURE) {
            for (uint32_t attempt = 1; attempt <= SERVICE_RESTART_LIMIT; attempt++) {
                os_sleep(SERVICE_RESTART_BACKOFF_TICKS);
                service->restart_count = attempt;
                service->restart_at = 0;
                os_printf("[serviced] auto-restart %s attempt=%u\n",
                          service->name,
                          attempt);
                result = start_service(service, 1);
                if (result == OS_SUCCESS) {
                    break;
                }
            }
            if (result < 0) {
                service->last_failure = OS_SERVICE_FAILURE_RESTART_LIMIT;
                service->restart_at = 0;
            }
        }
    } else if (request.command == OS_SERVICE_MANAGER_CMD_STOP) {
        result = stop_service(service, 1);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_RESTART) {
        result = restart_service(service);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_STATUS) {
        if (service == 0) {
            result = OS_ERR_NOT_FOUND;
        } else {
            supervise_services();
        }
    } else if (request.command == OS_SERVICE_MANAGER_CMD_HEALTH) {
        result = health_service(service);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_CRASH) {
        result = crash_service(service);
    } else if (request.command == OS_SERVICE_MANAGER_CMD_EXIT) {
        for (uint32_t i = MANAGED_SERVICE_COUNT; i > 0; i--) {
            stop_service(&services[i - 1], 0);
        }
    } else {
        result = OS_ERR_INVALID_ARGUMENT;
    }

    int should_exit = request.command == OS_SERVICE_MANAGER_CMD_EXIT && result == OS_SUCCESS;
    send_reply(message, &request, result, service);
    if (should_exit) {
        os_yield();
    }
    return should_exit;
}

int main(void) {
    if (validate_dependencies() < 0) {
        os_puts("[serviced] dependency graph invalid");
        return 1;
    }
    long result = os_service_register(OS_SERVICE_MANAGER_NAME, OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[serviced] register failed %ld\n", result);
        return 1;
    }

    os_printf("[serviced] ready pid=%u abi=%u\n",
              (uint32_t)os_getpid(),
              OS64_SERVICE_MANAGER_ABI_VERSION);
    while (1) {
        OsIpcMessage message;
        result = os_msg_wait_timeout(&message, SERVICE_SUPERVISOR_INTERVAL_TICKS);
        if (result == OS_ERR_TIMEOUT) {
            supervise_services();
            continue;
        }
        if (result < 0) {
            os_printf("[serviced] wait failed %ld\n", result);
            return 1;
        }
        if (message.type == OS_IPC_MESSAGE_REQUEST && handle_request(&message)) {
            break;
        }
    }

    os_service_unregister(OS_SERVICE_MANAGER_NAME);
    os_puts("[serviced] exit");
    return 0;
}
