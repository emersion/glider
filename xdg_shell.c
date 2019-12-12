#include <libliftoff.h>
#include <stdlib.h>
#include "allocator.h"
#include "server.h"
#include "wlr_buffer.h"
#include "xdg_shell.h"

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct glider_surface *surface = wl_container_of(listener, surface, destroy);
	liftoff_layer_destroy(surface->layer);
	wl_list_remove(&surface->destroy.link);
	wl_list_remove(&surface->commit.link);
	wl_list_remove(&surface->link);
	free(surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct glider_surface *surface = wl_container_of(listener, surface, commit);
	struct wlr_buffer *wlr_buffer = surface->wlr_surface->buffer;

	liftoff_layer_set_property(surface->layer, "FB_ID", 0);

	if (surface->primary_output == NULL || wlr_buffer == NULL) {
		return;
	}

	struct glider_buffer *buffer = glider_wlr_buffer_create(wlr_buffer);
	if (!glider_output_attach_buffer(surface->primary_output, buffer,
			surface->layer)) {
		glider_buffer_unlock(buffer);
		return;
	}

	glider_buffer_unlock(buffer);
}

void handle_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct glider_server *server =
		wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	struct glider_surface *surface = calloc(1, sizeof(*surface));
	surface->wlr_surface = xdg_surface->surface;
	surface->server = server;
	wl_list_insert(&server->surfaces, &surface->link);

	surface->destroy.notify = handle_surface_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &surface->destroy);

	surface->commit.notify = handle_surface_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &surface->commit);

	if (!wl_list_empty(&server->outputs)) {
		surface->primary_output = wl_container_of(server->outputs.prev,
			surface->primary_output, link);
		surface->layer =
			liftoff_layer_create(surface->primary_output->liftoff_output);
	}
}
