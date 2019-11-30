#include <stdlib.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "server.h"

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->key.link);
	free(keyboard);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct glider_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	wl_display_terminate(keyboard->server->display);
}

void handle_new_input(struct wl_listener *listener, void *data) {
	struct glider_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *dev = data;

	switch (dev->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:;
		wlr_log(WLR_DEBUG, "New keyboard '%s'", dev->name);

		struct glider_keyboard *keyboard = calloc(1, sizeof(*keyboard));
		keyboard->keyboard = dev->keyboard;
		keyboard->server = server;

		keyboard->destroy.notify = keyboard_handle_destroy;
		wl_signal_add(&keyboard->keyboard->events.destroy, &keyboard->destroy);

		keyboard->key.notify = keyboard_handle_key;
		wl_signal_add(&keyboard->keyboard->events.key, &keyboard->key);
		break;
	default:
		wlr_log(WLR_DEBUG, "Unhandled input device '%s'", dev->name);
		break;
	}
}
