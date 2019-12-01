#ifndef GLIDER_SWAPCHAIN_H
#define GLIDER_SWAPCHAIN_H

#include <stdbool.h>
#include <wayland-server-core.h>

#define GLIDER_SWAPCHAIN_CAP 3

struct glider_swapchain_slot {
	struct glider_buffer *buffer;
	bool acquired;

	struct wl_listener release;
	// TODO: buffer destroy listener
};

struct glider_swapchain {
	struct glider_allocator *allocator;

	int width, height;
	uint32_t format;
	// TODO: modifiers

	struct glider_swapchain_slot slots[GLIDER_SWAPCHAIN_CAP];

	// TODO: allocator destroy listener, destroy event
};

struct glider_swapchain *glider_swapchain_create(
	struct glider_allocator *alloc);
void glider_swapchain_destroy(struct glider_swapchain *swapchain);
struct glider_buffer *glider_swapchain_acquire(
	struct glider_swapchain *swapchain);

#endif
