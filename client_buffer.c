#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/render/wlr_texture.h>
#include "client_buffer.h"

static const struct glider_buffer_interface buffer_impl;

static struct glider_client_buffer *get_wlr_buffer_from_buffer(
		struct glider_buffer *buffer) {
	assert(buffer->impl == &buffer_impl);
	return (struct glider_client_buffer *)buffer;
}

static void buffer_handle_release(struct wl_listener *listener, void *data) {
	struct glider_client_buffer *buffer =
		wl_container_of(listener, buffer, release);
	if (buffer->client_buffer != NULL) {
		wlr_buffer_unref(&buffer->client_buffer->base);
	}
	buffer->client_buffer = NULL;
}

struct glider_buffer *glider_client_buffer_create(
		struct wlr_client_buffer *client_buffer) {
	if (client_buffer->texture == NULL) {
		return NULL;
	}

	int width, height;
	wlr_texture_get_size(client_buffer->texture, &width, &height);

	uint32_t format = DRM_FORMAT_INVALID;
	uint64_t modifier = DRM_FORMAT_MOD_INVALID;
	struct wlr_dmabuf_attributes dmabuf = {0};
	if (wlr_buffer_get_dmabuf(&client_buffer->base, &dmabuf)) {
		format = dmabuf.format;
		modifier = dmabuf.modifier;
	}

	struct glider_client_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	glider_buffer_init(&buffer->base, &buffer_impl, width, height,
		format, modifier);
	wlr_buffer_ref(&client_buffer->base);
	buffer->client_buffer = client_buffer;

	glider_buffer_lock(&buffer->base);
	glider_buffer_unref(&buffer->base);

	buffer->release.notify = buffer_handle_release;
	wl_signal_add(&buffer->base.events.release, &buffer->release);

	wlr_log(WLR_DEBUG, "Imported wlr_client_buffer %dx%d (format 0x%"PRIX32", "
		"modifier %"PRIX64")", width, height, format, modifier);

	return &buffer->base;
}

static void buffer_destroy(struct glider_buffer *glider_buffer) {
	struct glider_client_buffer *buffer =
		get_wlr_buffer_from_buffer(glider_buffer);
	if (buffer->client_buffer != NULL) {
		wlr_buffer_unref(&buffer->client_buffer->base);
	}
	free(buffer);
}

static bool buffer_get_dmabuf(struct glider_buffer *glider_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	struct glider_client_buffer *buffer =
		get_wlr_buffer_from_buffer(glider_buffer);
	if (buffer->client_buffer == NULL) {
		return false;
	}
	return wlr_buffer_get_dmabuf(&buffer->client_buffer->base, attribs);
}

static const struct glider_buffer_interface buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};
