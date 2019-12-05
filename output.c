#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "backend/backend.h"
#include "renderer.h"
#include "server.h"
#include "swapchain.h"
#include "xdg_shell.h"

static bool output_render_bg(struct glider_output *output,
		struct glider_buffer *buf) {
	struct glider_server *server = output->server;

	if (!glider_renderer_begin(server->renderer, buf)) {
		wlr_log(WLR_ERROR, "Failed to start rendering on buffer");
		return false;
	}
	wlr_renderer_clear(server->renderer->renderer,
		(float[4]){ 1.0, 0.0, 0.0, 1.0 });
	glider_renderer_end(server->renderer);
	return true;
}

bool glider_output_attach_buffer(struct glider_output *output,
		struct glider_buffer *buf, struct liftoff_layer *layer) {
	if (!glider_drm_connector_attach(output->output, buf, layer)) {
		wlr_log(WLR_ERROR, "Failed to attach buffer to layer");
		return false;
	}
	liftoff_layer_set_property(layer, "CRTC_X", 0);
	liftoff_layer_set_property(layer, "CRTC_Y", 0);
	liftoff_layer_set_property(layer, "CRTC_W", buf->width);
	liftoff_layer_set_property(layer, "CRTC_H", buf->height);
	liftoff_layer_set_property(layer, "SRC_X", 0);
	liftoff_layer_set_property(layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer, "SRC_W", buf->width << 16);
	liftoff_layer_set_property(layer, "SRC_H", buf->height << 16);
	return true;
}

static void output_push_frame(struct glider_output *output) {
	struct glider_buffer *buf = glider_swapchain_acquire(output->bg_swapchain);
	if (buf == NULL) {
		wlr_log(WLR_ERROR, "Failed to get next buffer");
		return;
	}
	if (!output_render_bg(output, buf)) {
		return;
	}
	if (!glider_output_attach_buffer(output, buf, output->bg_layer)) {
		return;
	}
	if (!glider_drm_connector_commit(output->output)) {
		wlr_log(WLR_ERROR, "Failed to commit connector");
		return;
	}
}

static bool output_test(struct glider_output *output) {
	struct glider_buffer *buf = glider_swapchain_acquire(output->bg_swapchain);
	if (buf == NULL) {
		wlr_log(WLR_ERROR, "Failed to get next buffer");
		return false;
	}
	if (!glider_output_attach_buffer(output, buf, output->bg_layer)) {
		return false;
	}
	if (!glider_drm_connector_test(output->output)) {
		wlr_log(WLR_DEBUG, "Connector test failed");
		return false;
	}
	return true;
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, destroy);
	glider_swapchain_destroy(output->bg_swapchain);
	liftoff_layer_destroy(output->bg_layer);
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
		wlr_surface_send_frame_done(surface->wlr_surface, &t);
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

	output->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	output->frame.notify = handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (!wlr_output_set_mode(wlr_output, mode)) {
		wlr_log(WLR_ERROR, "Modeset failed on output");
		return;
	}

	output->liftoff_output =
		glider_drm_connector_get_liftoff_output(output->output);
	if (output->liftoff_output == NULL) {
		wlr_log(WLR_ERROR, "Failed to get liftoff output");
		return;
	}

	output->bg_layer = liftoff_layer_create(output->liftoff_output);

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

	output->bg_swapchain = glider_swapchain_create(output->server->allocator,
		output->output->width, output->output->height, format);

	if (!output_test(output)) {
		wlr_log(WLR_DEBUG, "Failed to enable output, retrying without modifiers");
		glider_swapchain_destroy(output->bg_swapchain);
		struct wlr_drm_format format_no_modifiers = {
			.format = format->format,
		};
		output->bg_swapchain = glider_swapchain_create(output->server->allocator,
			output->output->width, output->output->height, &format_no_modifiers);
	}

	if (!output_test(output)) {
		wlr_log(WLR_ERROR, "Failed to enabled output");
		return;
	}

	output_push_frame(output);
}
