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

static struct glider_buffer *output_next_buffer(struct glider_output *output) {
	struct glider_output_buffer *free_buf = NULL;
	for (size_t i = 0; i < GLIDER_OUTPUT_BUFFERS_CAP; i++) {
		struct glider_output_buffer *buf = &output->buffers[i];
		if (buf->busy) {
			continue;
		}
		if (buf->buffer != NULL) {
			buf->busy = true;
			return buf->buffer;
		}
		free_buf = buf;
	}
	if (free_buf == NULL) {
		wlr_log(WLR_ERROR, "No free output buffer slot");
		return NULL;
	}

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
		return NULL;
	}

	wlr_log(WLR_DEBUG, "Allocating new buffer for output");
	free_buf->buffer = glider_allocator_create_buffer(output->server->allocator,
		output->output->width, output->output->height, format->format,
		format->modifiers, format->len);
	if (free_buf->buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate buffer");
		return NULL;
	}
	free_buf->busy = true;
	return free_buf->buffer;
}

static void output_push_frame(struct glider_output *output) {
	struct glider_server *server = output->server;
	struct liftoff_layer *layer = output->bg_layer;

	struct glider_buffer *buf = output_next_buffer(output);
	if (buf == NULL) {
		wlr_log(WLR_ERROR, "Failed to get next buffer");
		return;
	}
	if (!glider_renderer_begin(server->renderer, buf)) {
		wlr_log(WLR_ERROR, "Failed to start rendering on buffer");
		return;
	}
	wlr_renderer_clear(server->renderer->renderer,
		(float[4]){ 1.0, 0.0, 0.0, 1.0 });
	glider_renderer_end(server->renderer);

	int width = output->output->width;
	int height = output->output->height;
	if (!glider_drm_connector_attach(output->output, buf, layer)) {
		wlr_log(WLR_ERROR, "Failed to attach buffer to layer");
		return;
	}
	liftoff_layer_set_property(layer, "CRTC_X", 0);
	liftoff_layer_set_property(layer, "CRTC_Y", 0);
	liftoff_layer_set_property(layer, "CRTC_W", width);
	liftoff_layer_set_property(layer, "CRTC_H", height);
	liftoff_layer_set_property(layer, "SRC_X", 0);
	liftoff_layer_set_property(layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer, "SRC_W", width << 16);
	liftoff_layer_set_property(layer, "SRC_H", height << 16);

	if (!glider_drm_connector_commit(output->output)) {
		wlr_log(WLR_ERROR, "Failed to commit connector");
		return;
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, destroy);
	for (size_t i = 0; i < GLIDER_OUTPUT_BUFFERS_CAP; i++) {
		struct glider_output_buffer *buf = &output->buffers[i];
		glider_buffer_destroy(buf->buffer);
	}
	liftoff_layer_destroy(output->bg_layer);
	wl_list_remove(&output->destroy.link);
	free(output);
}

static void handle_frame(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, frame);
	output_push_frame(output);
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct glider_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	struct glider_output *output = calloc(1, sizeof(*output));
	output->output = wlr_output;
	output->server = server;

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

	output_push_frame(output);
}
