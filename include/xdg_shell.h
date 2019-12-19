#ifndef GLIDER_XDG_SHELL_H
#define GLIDER_XDG_SHELL_H

#include <wlr/types/wlr_xdg_shell.h>

struct glider_surface_output {
	struct glider_output *output;
	struct glider_surface *surface;
	struct liftoff_layer *layer;
	struct wl_list link; // glider_surface.outputs

	struct wl_listener destroy;
};

struct glider_surface {
	struct wlr_surface *wlr_surface;
	struct glider_server *server;
	struct wl_list link;

	// frame callbacks are sync'ed to this output
	struct glider_output *primary_output;
	struct wl_list outputs; // glider_surface_output.link

	struct wl_listener destroy;
	struct wl_listener commit;
};

struct glider_surface_output *glider_surface_get_output(
	struct glider_surface *surface, struct glider_output *output);

#endif
