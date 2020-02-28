#ifndef GLIDER_ALLOCATOR_H
#define GLIDER_ALLOCATOR_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct glider_allocator;

struct glider_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct glider_allocator *alloc,
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
struct wlr_buffer *glider_allocator_create_buffer(
	struct glider_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format);

// For glider_allocator implementors
void glider_allocator_init(struct glider_allocator *alloc,
	const struct glider_allocator_interface *impl);

#endif
