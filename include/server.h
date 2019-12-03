#ifndef GLIDER_SERVER_H
#define GLIDER_SERVER_H

#include <wayland-server-core.h>

struct glider_output {
	struct glider_server *server;
	struct wlr_output *output;

	struct liftoff_output *liftoff_output;

	struct glider_swapchain *bg_swapchain;
	struct liftoff_layer *bg_layer;

	struct wl_listener destroy;
	struct wl_listener frame;
};

struct glider_keyboard {
	struct glider_server *server;
	struct wlr_keyboard *keyboard;

	struct wl_listener destroy;
	struct wl_listener key;
};

struct glider_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct glider_allocator *allocator;
	struct glider_gbm_allocator *gbm_allocator;
	struct glider_renderer *renderer;
	struct wlr_xdg_shell *xdg_shell;

	struct wl_listener new_output;
	struct wl_listener new_input;
};

void handle_new_output(struct wl_listener *listener, void *data);
void handle_new_input(struct wl_listener *listener, void *data);

#endif
