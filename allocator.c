#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "gbm_allocator.h"

void glider_allocator_init(struct glider_allocator *alloc,
		const struct glider_allocator_interface *impl) {
	assert(impl && impl->destroy && impl->create_buffer);
	alloc->impl = impl;
	wl_signal_init(&alloc->events.destroy);
}

void glider_allocator_destroy(struct glider_allocator *alloc) {
	wl_signal_emit(&alloc->events.destroy, NULL);
	alloc->impl->destroy(alloc);
}

struct glider_buffer *glider_allocator_create_buffer(
		struct glider_allocator *alloc, int width, int height,
		const struct wlr_drm_format *format) {
	return alloc->impl->create_buffer(alloc, width, height, format);
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
	buffer->n_refs = 1;
	wl_signal_init(&buffer->events.destroy);
	wl_signal_init(&buffer->events.release);
}

static void buffer_ref(struct glider_buffer *buffer) {
	buffer->n_refs++;
}

void glider_buffer_unref(struct glider_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	assert(buffer->n_refs > 0);
	buffer->n_refs--;
	if (buffer->n_refs > 0) {
		return;
	}

	wl_signal_emit(&buffer->events.destroy, NULL);
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
	buffer_ref(buffer);
}

void glider_buffer_unlock(struct glider_buffer *buffer) {
	assert(buffer->n_locks > 0);
	buffer->n_locks--;
	if (buffer->n_locks == 0) {
		wl_signal_emit(&buffer->events.release, NULL);
	}

	glider_buffer_unref(buffer);
}
