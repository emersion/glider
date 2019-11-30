#ifndef GLIDER_ALLOCATOR_H
#define GLIDER_ALLOCATOR_H

#include <gbm.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>

// TODO: turn this into an interface
struct glider_buffer {
	struct gbm_bo *gbm_bo;
	struct wlr_dmabuf_attributes dmabuf_attribs;

	struct {
		struct wl_signal release;
	} events;
};

// TODO: turn this into an interface
struct glider_allocator {
	struct gbm_device *gbm_device;
};

void glider_allocator_destroy(struct glider_allocator *alloc);
struct glider_buffer *glider_allocator_create_buffer(
	struct glider_allocator *alloc, int width, int height, uint32_t format,
	const uint64_t *modifiers, size_t modifiers_len);

void glider_buffer_destroy(struct glider_buffer *buffer);
bool glider_buffer_get_dmabuf(struct glider_buffer *buffer,
	struct wlr_dmabuf_attributes *attribs);

struct glider_allocator *glider_gbm_allocator_create(int render_fd);

#endif
