#include <stdlib.h>
#include "server.h"
#include "xdg_shell.h"

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct glider_surface *surface = wl_container_of(listener, surface, destroy);
	wl_list_remove(&surface->destroy.link);
	free(surface);
}

void handle_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct glider_server *server =
		wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	struct glider_surface *surface = calloc(1, sizeof(*surface));
	surface->wlr_surface = xdg_surface->surface;

	surface->destroy.notify = handle_surface_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &surface->destroy);
}
