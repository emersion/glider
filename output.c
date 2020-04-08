#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "backend/backend.h"
#include "gl_renderer.h"
#include "server.h"
#include "swapchain.h"
#include "surface.h"

static bool output_render_bg(struct glider_output *output,
		struct wlr_buffer *buf) {
	struct glider_server *server = output->server;

	if (!glider_gl_renderer_begin(server->renderer, buf)) {
		wlr_log(WLR_ERROR, "Failed to start rendering on buffer");
		return false;
	}
	wlr_renderer_clear(server->renderer->renderer,
		(float[4]){ 1.0, 0.0, 0.0, 1.0 });
	glider_gl_renderer_end(server->renderer);
	return true;
}

static bool output_needs_render(struct glider_output *output) {
	if (liftoff_layer_get_plane_id(output->bg_layer) == 0) {
		return true;
	}

	struct glider_surface *surface;
	wl_list_for_each(surface, &output->server->surfaces, link) {
		struct glider_surface_output *so =
			glider_surface_get_output(surface, output);
		if (so != NULL && liftoff_layer_get_plane_id(so->layer) == 0) {
			return true;
		}
	}

	return false;
}

static bool output_render(struct glider_output *output,
		struct wlr_buffer *buf) {
	struct glider_server *server = output->server;

	wlr_log(WLR_DEBUG, "Rendering output");

	if (!glider_gl_renderer_begin(server->renderer, buf)) {
		wlr_log(WLR_ERROR, "Failed to start rendering on buffer");
		return false;
	}

	wlr_renderer_clear(server->renderer->renderer,
		(float[4]){ 0.0, 1.0, 0.0, 1.0 });

	struct glider_surface *surface;
	wl_list_for_each(surface, &output->server->surfaces, link) {
		struct glider_surface_output *so =
			glider_surface_get_output(surface, output);
		if (so == NULL || liftoff_layer_get_plane_id(so->layer) != 0) {
			continue;
		}

		struct wlr_texture *texture =
			wlr_surface_get_texture(surface->wlr_surface);
		if (texture == NULL) {
			continue;
		}

		wlr_render_texture(server->renderer->renderer, texture,
			output->output->transform_matrix, 0, 0, 1.0);
	}

	glider_gl_renderer_end(server->renderer);
	return true;
}

bool glider_output_attach_buffer(struct glider_output *output,
		struct wlr_buffer *buf, struct liftoff_layer *layer) {
	liftoff_layer_set_property(layer, "CRTC_X", 0);
	liftoff_layer_set_property(layer, "CRTC_Y", 0);
	liftoff_layer_set_property(layer, "CRTC_W", buf->width);
	liftoff_layer_set_property(layer, "CRTC_H", buf->height);
	liftoff_layer_set_property(layer, "SRC_X", 0);
	liftoff_layer_set_property(layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer, "SRC_W", buf->width << 16);
	liftoff_layer_set_property(layer, "SRC_H", buf->height << 16);
	if (!glider_drm_connector_attach(output->output, buf, layer)) {
		wlr_log(WLR_ERROR, "Failed to attach buffer to layer");
		return false;
	}
	return true;
}

static bool output_test(struct glider_output *output) {
	struct wlr_buffer *buf = glider_swapchain_acquire(output->swapchain);
	if (buf == NULL) {
		wlr_log(WLR_ERROR, "Failed to get next buffer");
		return false;
	}
	if (!glider_output_attach_buffer(output, buf, output->composition_layer)) {
		goto error_buffer;
	}
	if (!wlr_output_test(output->output)) {
		wlr_log(WLR_DEBUG, "Connector test failed");
		goto error_buffer;
	}
	wlr_buffer_unlock(buf);
	return true;

error_buffer:
	wlr_buffer_unlock(buf);
	return false;
}

static void output_push_frame(struct glider_output *output) {
	if (!output_test(output)) {
		return;
	}

	struct wlr_buffer *buf = NULL;
	if (output_needs_render(output)) {
		buf = glider_swapchain_acquire(output->swapchain);
		if (buf == NULL) {
			wlr_log(WLR_ERROR, "Failed to get next buffer");
			return;
		}
		if (!output_render(output, buf)) {
			goto out;
		}
		if (!glider_output_attach_buffer(output, buf, output->composition_layer)) {
			goto out;
		}
	}

	if (!wlr_output_commit(output->output)) {
		wlr_log(WLR_ERROR, "Failed to commit connector");
		goto out;
	}

out:
	if (buf != NULL) {
		wlr_buffer_unlock(buf);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, destroy);
	wl_signal_emit(&output->events.destroy, NULL);
	wlr_buffer_drop(output->bg_buffer);
	liftoff_layer_destroy(output->bg_layer);
	glider_swapchain_destroy(output->swapchain);
	liftoff_layer_destroy(output->composition_layer);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void handle_frame(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, frame);
	output_push_frame(output);

	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);

	struct glider_surface *surface;
	wl_list_for_each(surface, &output->server->surfaces, link) {
		if (surface->primary_output == output) {
			wlr_surface_send_frame_done(surface->wlr_surface, &t);
		}
	}
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct glider_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	struct glider_output *output = calloc(1, sizeof(*output));
	output->output = wlr_output;
	output->server = server;
	wl_list_insert(&server->outputs, &output->link);
	wl_signal_init(&output->events.destroy);

	output->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	output->frame.notify = handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	// TODO: push the first frame on modeset (this requires allocating the CRTC
	// before making use of libliftoff)
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	wlr_output_enable(wlr_output, true);
	wlr_output_set_mode(wlr_output, mode);
	if (!wlr_output_commit(wlr_output)) {
		wlr_log(WLR_ERROR, "Failed to modeset output");
		return;
	}

	output->liftoff_output =
		glider_drm_connector_get_liftoff_output(output->output);
	if (output->liftoff_output == NULL) {
		wlr_log(WLR_ERROR, "Failed to get liftoff output");
		return;
	}

	output->composition_layer = liftoff_layer_create(output->liftoff_output);

	const struct wlr_drm_format_set *formats =
		glider_drm_connector_get_primary_formats(output->output);
	assert(formats != NULL);

	const struct wlr_drm_format *format =
		wlr_drm_format_set_get(formats, DRM_FORMAT_ARGB8888);
	if (format == NULL) {
		format = wlr_drm_format_set_get(formats, DRM_FORMAT_XRGB8888);
	}
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Unsupported output formats");
		return;
	}

	struct wlr_drm_format format_no_modifiers = { .format = format->format };

	output->swapchain = glider_swapchain_create(output->server->allocator,
		output->output->width, output->output->height, format);

	if (!output_test(output)) {
		wlr_log(WLR_DEBUG, "Failed to enable output, retrying without modifiers");
		format = &format_no_modifiers;

		glider_swapchain_destroy(output->swapchain);
		output->swapchain = glider_swapchain_create(
			output->server->allocator, output->output->width,
			output->output->height, &format_no_modifiers);

		if (!output_test(output)) {
			wlr_log(WLR_ERROR, "Failed to enable output");
			return;
		}
	}

	liftoff_output_set_composition_layer(output->liftoff_output,
		output->composition_layer);

	output->bg_layer = liftoff_layer_create(output->liftoff_output);
	liftoff_layer_set_property(output->bg_layer, "zpos", 1);

	output->bg_buffer = glider_allocator_create_buffer(server->allocator,
		output->output->width, output->output->height, format);
	if (output->bg_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to create background buffer");
		return;
	}
	if (!output_render_bg(output, output->bg_buffer)) {
		return;
	}
	if (!glider_output_attach_buffer(output, output->bg_buffer,
			output->bg_layer)) {
		return;
	}
}
