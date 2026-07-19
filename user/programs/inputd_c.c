#include <os64/os64.h>

#define INPUT_WAIT_SLICE_TICKS 10u

static OsProcessIdentity window_owner;
static uint32_t next_input_sequence = 1;
static uint32_t dropped_events;

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

static void send_status_reply(OsProcessIdentity target,
                              const OsServiceQueryRequest* request,
                              int result) {
    OsInputServiceStatusReply reply;
    reply.size = sizeof(reply);
    reply.command = request != 0 ? request->command : OS_SERVICE_QUERY_STATUS;
    reply.result = result;
    reply.request_id = request != 0 ? request->request_id : 0;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = result == OS_SUCCESS ? 1u : 0u;
    reply.capabilities = OS_SERVICE_CAP_KEYBOARD;
    reply.reserved = dropped_events;

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(target, &message);
}

static void handle_request(const OsIpcMessage* message) {
    OsServiceQueryRequest request;
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length != sizeof(request)) {
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request)) {
        return;
    }

    if (request.command == OS_SERVICE_QUERY_HEALTH) {
        OsServiceHealthReply reply;
        reply.size = sizeof(reply);
        reply.command = request.command;
        reply.result = OS_SUCCESS;
        reply.request_id = request.request_id;
        reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
        reply.ready = 1;
        reply.health = OS_SERVICE_HEALTH_HEALTHY;
        reply.reserved = dropped_events;
        OsIpcMessage response;
        os_msg_init(&response, OS_IPC_MESSAGE_REPLY);
        response.length = sizeof(reply);
        os_memcpy(response.payload, &reply, sizeof(reply));
        os_msg_send_to_identity(os_msg_sender_identity(message), &response);
        return;
    }
    int result = request.command == OS_SERVICE_QUERY_STATUS
        ? OS_SUCCESS : OS_ERR_UNSUPPORTED;
    send_status_reply(os_msg_sender_identity(message), &request, result);
}

static void drain_service_requests(void) {
    OsIpcMessage message;
    while (os_msg_recv(&message) == OS_SUCCESS) {
        handle_request(&message);
    }
}

static long refresh_window_owner(void) {
    OsProcessIdentity current;
    long result = os_service_find_owner_identity("window", &current);
    if (result < 0) {
        window_owner.pid = 0;
        window_owner.generation = 0;
        return result;
    }
    if (!identity_equal(current, window_owner)) {
        window_owner = current;
        os_printf("[inputd] window connected pid=%u generation=%u\n",
                  current.pid, current.generation);
    }
    return OS_SUCCESS;
}

static void forward_event(const OsInputEvent* event) {
    if (event == 0 || event->size != sizeof(*event) ||
        event->type != OS_INPUT_EVENT_KEY) {
        return;
    }
    if (refresh_window_owner() < 0) {
        dropped_events++;
        return;
    }
    OsWindowInputForward forward;
    forward.size = sizeof(forward);
    forward.abi_version = OS64_WINDOW_ABI_VERSION;
    forward.command = OS_WINDOW_INPUT_EVENT;
    forward.flags = 0;
    forward.input_sequence = next_input_sequence++;
    if (next_input_sequence == 0) {
        next_input_sequence = 1;
    }
    forward.reserved = 0;
    forward.event = *event;

    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_EVENT);
    message.length = sizeof(forward);
    os_memcpy(message.payload, &forward, sizeof(forward));
    long result = os_msg_v2_send_to_identity(window_owner, &message);
    if (result < 0) {
        dropped_events++;
        if (result == OS_ERR_QUEUE_FULL) {
            return;
        }
        if (result == OS_ERR_NOT_FOUND) {
            window_owner.pid = 0;
            window_owner.generation = 0;
        }
    }
}

int main(void) {
    window_owner.pid = 0;
    window_owner.generation = 0;
    long result = os_service_register("input", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[inputd] register failed %ld\n", result);
        return 1;
    }

    os_printf("[inputd] ready pid=%u raw-owner=1\n", (uint32_t)os_getpid());
    while (1) {
        drain_service_requests();
        if (window_owner.pid == 0) {
            if (refresh_window_owner() < 0) {
                OsIpcMessage message;
                result = os_msg_wait(&message);
                if (result < 0) {
                    os_printf("[inputd] service wait failed %ld\n", result);
                    return 1;
                }
                handle_request(&message);
                continue;
            }
            OsInputEvent stale;
            while (os_input_poll(&stale) == OS_SUCCESS) {
            }
            os_sleep(1);
            continue;
        }
        OsInputEvent event;
        result = os_input_wait_timeout(&event, INPUT_WAIT_SLICE_TICKS);
        if (result == OS_SUCCESS) {
            forward_event(&event);
            os_yield();
        } else if (result == OS_ERR_NOT_READY) {
            os_sleep(1);
        } else if (result != OS_ERR_TIMEOUT && result != OS_ERR_WOULD_BLOCK &&
                   result != OS_ERR_CANCELLED) {
            os_printf("[inputd] input wait failed %ld\n", result);
            return 1;
        }
    }
}
