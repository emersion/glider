#ifndef GLIDER_SERVER_H
#define GLIDER_SERVER_H

#include <wayland-server-core.h>

struct glider_output {
	struct wlr_output *output;

	struct wl_listener destroy;
};

struct glider_server {
	struct wl_display *display;
	struct wlr_backend *backend;

	struct wl_listener new_output;
};

void handle_new_output(struct wl_listener *listener, void *data);

#endif
