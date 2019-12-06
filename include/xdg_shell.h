#ifndef GLIDER_XDG_SHELL_H
#define GLIDER_XDG_SHELL_H

#include <wlr/types/wlr_xdg_shell.h>

struct glider_surface {
	struct wlr_surface *wlr_surface;
	struct glider_server *server;
	struct wl_list link;

	struct glider_buffer *pending_buffer;

	struct glider_output *primary_output;
	// TODO: support being displayed on more than one output
	struct liftoff_layer *layer;

	struct wl_listener destroy;
	struct wl_listener commit;
};

#endif
