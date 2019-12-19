#ifndef GLIDER_SERVER_H
#define GLIDER_SERVER_H

#include <wayland-server-core.h>

struct glider_output {
	struct glider_server *server;
	struct wlr_output *output;
	struct wl_list link; // glider_server.outputs

	struct liftoff_output *liftoff_output;

	struct glider_buffer *bg_buffer;
	struct liftoff_layer *bg_layer;

	struct glider_swapchain *swapchain;
	struct liftoff_layer *composition_layer;

	struct {
		struct wl_signal destroy;
	} events;

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
	struct wlr_xdg_shell *xdg_shell;

	struct wl_list outputs; // glider_output.link
	struct wl_list surfaces; // glider_surface.link

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_xdg_surface;
};

void handle_new_output(struct wl_listener *listener, void *data);
void handle_new_input(struct wl_listener *listener, void *data);
void handle_new_xdg_surface(struct wl_listener *listener, void *data);

struct glider_buffer;

bool glider_output_attach_buffer(struct glider_output *output,
	struct glider_buffer *buf, struct liftoff_layer *layer);

#endif
