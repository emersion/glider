#include <libliftoff.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "allocator.h"
#include "server.h"
#include "client_buffer.h"
#include "surface.h"

static void surface_output_destroy(struct glider_surface_output *so) {
	if (so->surface->primary_output == so->output) {
		so->surface->primary_output = NULL;
	}
	wl_list_remove(&so->link);
	wl_list_remove(&so->destroy.link);
	liftoff_layer_destroy(so->layer);
	free(so);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct glider_surface_output *so = wl_container_of(listener, so, destroy);
	surface_output_destroy(so);
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct glider_surface *surface = wl_container_of(listener, surface, destroy);

	struct glider_surface_output *so, *so_tmp;
	wl_list_for_each_safe(so, so_tmp, &surface->outputs, link) {
		surface_output_destroy(so);
	}

	wl_list_remove(&surface->destroy.link);
	wl_list_remove(&surface->commit.link);
	wl_list_remove(&surface->link);
	free(surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct glider_surface *surface = wl_container_of(listener, surface, commit);
	struct wlr_client_buffer *client_buffer = surface->wlr_surface->buffer;

	if (client_buffer == NULL) {
		return;
	}

	struct glider_buffer *buffer = glider_client_buffer_create(client_buffer);

	struct glider_surface_output *so;
	wl_list_for_each(so, &surface->outputs, link) {
		liftoff_layer_set_fb_composited(so->layer);
		liftoff_layer_set_property(so->layer, "zpos", 2);

		glider_output_attach_buffer(so->output, buffer, so->layer);
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
	wl_list_init(&surface->outputs);

	surface->destroy.notify = handle_surface_destroy;
	wl_signal_add(&xdg_surface->surface->events.destroy, &surface->destroy);

	surface->commit.notify = handle_surface_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &surface->commit);

	// TODO: create surface_output on hotplug too
	struct glider_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->liftoff_output == NULL) {
			continue;
		}

		struct glider_surface_output *so = calloc(1, sizeof(*so));
		so->output = output;
		so->surface = surface;
		so->layer = liftoff_layer_create(output->liftoff_output);
		wl_list_insert(&surface->outputs, &so->link);

		so->destroy.notify = handle_output_destroy;
		wl_signal_add(&output->events.destroy, &so->destroy);
	}

	if (!wl_list_empty(&server->outputs)) {
		surface->primary_output = wl_container_of(server->outputs.prev,
			surface->primary_output, link);
	}
}

struct glider_surface_output *glider_surface_get_output(
		struct glider_surface *surface, struct glider_output *output) {
	struct glider_surface_output *so;
	wl_list_for_each(so, &surface->outputs, link) {
		if (so->output == output) {
			return so;
		}
	}
	return NULL;
}
