#ifndef GLIDER_SWAPCHAIN_H
#define GLIDER_SWAPCHAIN_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/drm_format_set.h>

#define GLIDER_SWAPCHAIN_CAP 3

struct glider_swapchain_slot {
	struct glider_buffer *buffer;
	bool acquired;

	struct wl_listener destroy;
	struct wl_listener release;
};

struct glider_swapchain {
	struct glider_allocator *allocator;

	int width, height;
	struct wlr_drm_format *format;

	struct glider_swapchain_slot slots[GLIDER_SWAPCHAIN_CAP];

	// TODO: allocator destroy listener, destroy event
};

struct glider_swapchain *glider_swapchain_create(
	struct glider_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format);
void glider_swapchain_destroy(struct glider_swapchain *swapchain);
struct glider_buffer *glider_swapchain_acquire(
	struct glider_swapchain *swapchain);
// TODO: add glider_swapchain_resize

#endif
