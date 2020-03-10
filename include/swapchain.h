#ifndef GLIDER_SWAPCHAIN_H
#define GLIDER_SWAPCHAIN_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/drm_format_set.h>

#define GLIDER_SWAPCHAIN_CAP 3

struct glider_swapchain_slot {
	struct glider_buffer *buffer;
	bool acquired; // waiting for release

	struct wl_listener release;
};

struct glider_swapchain {
	struct glider_allocator *allocator; // NULL if destroyed

	int width, height;
	struct wlr_drm_format *format;

	struct glider_swapchain_slot slots[GLIDER_SWAPCHAIN_CAP];

	struct wl_listener allocator_destroy;
};

struct glider_swapchain *glider_swapchain_create(
	struct glider_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format);
void glider_swapchain_destroy(struct glider_swapchain *swapchain);
/**
 * Acquire a buffer from the swap chain.
 *
 * The returned buffer is locked. When the caller is done with it, they must
 * unlock it.
 */
struct glider_buffer *glider_swapchain_acquire(
	struct glider_swapchain *swapchain);
bool glider_swapchain_resize(struct glider_swapchain *swapchain,
	int width, int height);

#endif
