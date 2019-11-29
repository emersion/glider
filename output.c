#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "server.h"
#include "backend/backend.h"

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

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, destroy);
	for (size_t i = 0; i < GLIDER_OUTPUT_BUFFERS_CAP; i++) {
		struct glider_output_buffer *buf = &output->buffers[i];
		glider_buffer_destroy(buf->buffer);
	}
	wl_list_remove(&output->destroy.link);
	free(output);
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

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (!wlr_output_set_mode(wlr_output, mode)) {
		wlr_log(WLR_ERROR, "Modeset failed on output");
		return;
	}

	output_next_buffer(output);
}
