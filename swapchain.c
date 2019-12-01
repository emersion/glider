#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "swapchain.h"

struct glider_swapchain *glider_swapchain_create(
		struct glider_allocator *alloc) {
	struct glider_swapchain *swapchain = calloc(1, sizeof(*swapchain));
	if (swapchain == NULL) {
		return NULL;
	}
	swapchain->allocator = alloc;
	return swapchain;
}

void glider_swapchain_destroy(struct glider_swapchain *swapchain) {
	for (size_t i = 0; i < GLIDER_SWAPCHAIN_CAP; i++) {
		glider_buffer_destroy(swapchain->slots[i].buffer);
	}
	free(swapchain);
}

static void slot_handle_release(struct wl_listener *listener,
		void *data) {
	struct glider_swapchain_slot *slot =
		wl_container_of(listener, slot, release);
	wl_list_remove(&slot->release.link);
	slot->acquired = false;
}

static struct glider_buffer *slot_acquire(struct glider_swapchain_slot *slot) {
	assert(!slot->acquired);
	assert(slot->buffer != NULL);

	slot->acquired = true;

	slot->release.notify = slot_handle_release;
	wl_signal_add(&slot->buffer->events.release, &slot->release);

	return slot->buffer;
}

struct glider_buffer *glider_swapchain_acquire(
		struct glider_swapchain *swapchain) {
	struct glider_swapchain_slot *free_slot = NULL;
	for (size_t i = 0; i < GLIDER_SWAPCHAIN_CAP; i++) {
		struct glider_swapchain_slot *slot = &swapchain->slots[i];
		if (slot->acquired) {
			continue;
		}
		if (slot->buffer != NULL) {
			return slot_acquire(slot);
		}
		free_slot = slot;
	}
	if (free_slot == NULL) {
		wlr_log(WLR_ERROR, "No free output buffer slot");
		return NULL;
	}

	wlr_log(WLR_DEBUG, "Allocating new swapchain buffer");
	free_slot->buffer = glider_allocator_create_buffer(swapchain->allocator,
		swapchain->width, swapchain->height, swapchain->format, NULL, 0);
	if (free_slot->buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate buffer");
		return NULL;
	}
	return slot_acquire(free_slot);
}
