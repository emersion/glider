#ifndef GLIDER_ALLOCATOR_H
#define GLIDER_ALLOCATOR_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct glider_buffer;

struct glider_buffer_interface {
	bool (*get_dmabuf)(struct glider_buffer *buf,
		struct wlr_dmabuf_attributes *attribs);
	void (*destroy)(struct glider_buffer *buf);
};

struct glider_buffer {
	const struct glider_buffer_interface *impl;

	int width, height;
	uint32_t format;
	uint64_t modifier;

	size_t n_locks;

	struct {
		struct wl_signal destroy;
		struct wl_signal release;
	} events;

	struct wlr_dmabuf_attributes dmabuf_attribs; // cached DMA-BUF attribs
};

struct glider_allocator;

struct glider_allocator_interface {
	struct glider_buffer *(*create_buffer)(struct glider_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format);
	void (*destroy)(struct glider_allocator *alloc);
};

struct glider_allocator {
	const struct glider_allocator_interface *impl;
	struct {
		struct wl_signal destroy;
	} events;
};

void glider_allocator_destroy(struct glider_allocator *alloc);
struct glider_buffer *glider_allocator_create_buffer(
	struct glider_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format);

void glider_buffer_destroy(struct glider_buffer *buffer);
bool glider_buffer_get_dmabuf(struct glider_buffer *buffer,
	struct wlr_dmabuf_attributes *attribs);
void glider_buffer_lock(struct glider_buffer *buffer);
void glider_buffer_unlock(struct glider_buffer *buffer);

// For glider_buffer implementors
void glider_buffer_init(struct glider_buffer *buffer,
	const struct glider_buffer_interface *impl, int width, int height,
	uint32_t format, uint64_t modifier);

// For glider_allocator implementors
void glider_allocator_init(struct glider_allocator *alloc,
	const struct glider_allocator_interface *impl);

#endif
