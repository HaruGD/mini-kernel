#include <os64/os64.h>

static void print_key_event(const OsInputEvent* event) {
    if (event == 0 || event->type != OS_INPUT_EVENT_KEY) {
        return;
    }

    const OsKeyEvent* key = &event->data.key;
    os_printf("event key type=%u keycode=%u char=%u mods=%u\n",
              key->type,
              key->keycode,
              key->character,
              key->modifiers);
}

int main(void) {
    os_puts("=== OS64 event loop sample ===");
    os_puts("[uevent] waiting for key event");
    os_puts("[uevent] press q or Enter to exit");

    while (1) {
        OsInputEvent event;
        long result = os_input_wait(&event);
        if (result < 0) {
            os_printf("[uevent] wait failed result=%ld\n", result);
            return 1;
        }
        if (event.type != OS_INPUT_EVENT_KEY) {
            continue;
        }

        print_key_event(&event);
        if (event.data.key.type != OS_KEY_EVENT_DOWN) {
            continue;
        }
        if (event.data.key.character == 'q' ||
            event.data.key.character == 'Q' ||
            event.data.key.keycode == OS_KEY_ENTER) {
            os_puts("[uevent] exit key received");
            return 0;
        }
    }
}
