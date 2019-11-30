#ifndef GLIDER_SERVER_H
#define GLIDER_SERVER_H

#include <wayland-server-core.h>

#define GLIDER_OUTPUT_BUFFERS_CAP 3

struct glider_output_buffer {
	struct glider_buffer *buffer;
	bool busy;

	struct wl_listener release;
};

struct glider_output {
	struct glider_server *server;
	struct wlr_output *output;

	struct glider_output_buffer buffers[GLIDER_OUTPUT_BUFFERS_CAP];

	struct liftoff_output *liftoff_output;
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
	struct glider_renderer *renderer;

	struct wl_listener new_output;
	struct wl_listener new_input;
};

void handle_new_output(struct wl_listener *listener, void *data);
void handle_new_input(struct wl_listener *listener, void *data);

#endif
