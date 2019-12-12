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

	size_t n_refs, n_locks;

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
/**
 * Allocate a new buffer.
 *
 * The returned buffer is referenced. When the caller is done with it, they
 * must unreference it.
 */
struct glider_buffer *glider_allocator_create_buffer(
	struct glider_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format);

void glider_buffer_unref(struct glider_buffer *buffer);
bool glider_buffer_get_dmabuf(struct glider_buffer *buffer,
	struct wlr_dmabuf_attributes *attribs);
/**
 * Mark the buffer as in-use.
 *
 * Buffer consumers call this function while they need to read the buffer, then
 * unlock it. The buffer won't be destroyed while locked.
 */
void glider_buffer_lock(struct glider_buffer *buffer);
/**
 * Release the buffer.
 *
 * This can destroy the buffer.
 */
void glider_buffer_unlock(struct glider_buffer *buffer);

// For glider_buffer implementors
void glider_buffer_init(struct glider_buffer *buffer,
	const struct glider_buffer_interface *impl, int width, int height,
	uint32_t format, uint64_t modifier);

// For glider_allocator implementors
void glider_allocator_init(struct glider_allocator *alloc,
	const struct glider_allocator_interface *impl);

#endif
