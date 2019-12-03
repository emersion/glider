#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "gbm_allocator.h"

struct glider_allocator *glider_gbm_allocator_create(int fd) {
	struct glider_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wl_signal_init(&alloc->events.destroy);

	alloc->gbm_device = gbm_create_device(fd);
	if (alloc->gbm_device == NULL) {
		wlr_log(WLR_ERROR, "gbm_create_device failed");
		free(alloc);
		return NULL;
	}

	return alloc;
}

void glider_allocator_destroy(struct glider_allocator *alloc) {
	wl_signal_emit(&alloc->events.destroy, NULL);
	gbm_device_destroy(alloc->gbm_device);
	free(alloc);
}

struct glider_buffer *glider_allocator_create_buffer(
		struct glider_allocator *alloc, int width, int height,
		const struct wlr_drm_format *format) {
	return glider_gbm_buffer_create(alloc->gbm_device, width, height, format);
}

void glider_buffer_init(struct glider_buffer *buffer,
		const struct glider_buffer_interface *impl, int width, int height,
		uint32_t format, uint64_t modifier) {
	assert(impl && impl->destroy);
	buffer->impl = impl;
	buffer->width = width;
	buffer->height = height;
	buffer->format = format;
	buffer->modifier = modifier;
	wl_signal_init(&buffer->events.destroy);
	wl_signal_init(&buffer->events.release);
}

void glider_buffer_destroy(struct glider_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	wl_signal_emit(&buffer->events.destroy, NULL);
	if (buffer->dmabuf_attribs.n_planes > 0) {
		wlr_dmabuf_attributes_finish(&buffer->dmabuf_attribs);
	}
	buffer->impl->destroy(buffer);
}

bool glider_buffer_get_dmabuf(struct glider_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs) {
	if (buffer->dmabuf_attribs.n_planes == 0) {
		if (!buffer->impl->get_dmabuf) {
			return false;
		}
		if (!buffer->impl->get_dmabuf(buffer, &buffer->dmabuf_attribs)) {
			return false;
		}
	}

	memcpy(attribs, &buffer->dmabuf_attribs,
		sizeof(struct wlr_dmabuf_attributes));
	return true;
}

void glider_buffer_lock(struct glider_buffer *buffer) {
	buffer->n_locks++;
}

void glider_buffer_unlock(struct glider_buffer *buffer) {
	assert(buffer->n_locks > 0);
	buffer->n_locks--;
	if (buffer->n_locks == 0) {
		wl_signal_emit(&buffer->events.release, NULL);
	}
}
