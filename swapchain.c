#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "swapchain.h"

static void swapchain_handle_allocator_destroy(struct wl_listener *listener,
		void *data) {
	struct glider_swapchain *swapchain =
		wl_container_of(listener, swapchain, allocator_destroy);
	swapchain->allocator = NULL;
}

struct glider_swapchain *glider_swapchain_create(
		struct glider_allocator *alloc, int width, int height,
		const struct wlr_drm_format *format) {
	struct glider_swapchain *swapchain = calloc(1, sizeof(*swapchain));
	if (swapchain == NULL) {
		return NULL;
	}
	swapchain->allocator = alloc;
	swapchain->width = width;
	swapchain->height = height;

	// TODO: extract this in wlr_drm_format_dup
	size_t format_size = sizeof(struct wlr_drm_format) +
		format->len * sizeof(format->modifiers[0]);
	swapchain->format = malloc(format_size);
	if (swapchain->format == NULL) {
		free(swapchain);
		return NULL;
	}
	memcpy(swapchain->format, format, format_size);

	swapchain->allocator_destroy.notify = swapchain_handle_allocator_destroy;
	wl_signal_add(&alloc->events.destroy, &swapchain->allocator_destroy);

	return swapchain;
}

static void slot_reset(struct glider_swapchain_slot *slot) {
	if (slot->acquired) {
		wl_list_remove(&slot->destroy.link);
		wl_list_remove(&slot->release.link);
	}
	glider_buffer_unref(slot->buffer);
	memset(slot, 0, sizeof(*slot));
}

void glider_swapchain_destroy(struct glider_swapchain *swapchain) {
	for (size_t i = 0; i < GLIDER_SWAPCHAIN_CAP; i++) {
		slot_reset(&swapchain->slots[i]);
	}
	wl_list_remove(&swapchain->allocator_destroy.link);
	free(swapchain->format);
	free(swapchain);
}

static void slot_release(struct glider_swapchain_slot *slot) {
	wl_list_remove(&slot->destroy.link);
	wl_list_remove(&slot->release.link);
	slot->acquired = false;
}

static void slot_handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_swapchain_slot *slot =
		wl_container_of(listener, slot, destroy);
	slot_release(slot);
	slot->buffer = NULL;
}

static void slot_handle_release(struct wl_listener *listener, void *data) {
	struct glider_swapchain_slot *slot =
		wl_container_of(listener, slot, release);
	slot_release(slot);
}

static struct glider_buffer *slot_acquire(struct glider_swapchain_slot *slot) {
	assert(!slot->acquired);
	assert(slot->buffer != NULL);

	slot->acquired = true;

	slot->destroy.notify = slot_handle_destroy;
	wl_signal_add(&slot->buffer->events.destroy, &slot->destroy);

	slot->release.notify = slot_handle_release;
	wl_signal_add(&slot->buffer->events.release, &slot->release);

	glider_buffer_lock(slot->buffer);
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

	if (swapchain->allocator == NULL) {
		return NULL;
	}

	wlr_log(WLR_DEBUG, "Allocating new swapchain buffer");
	free_slot->buffer = glider_allocator_create_buffer(swapchain->allocator,
		swapchain->width, swapchain->height, swapchain->format);
	if (free_slot->buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate buffer");
		return NULL;
	}
	return slot_acquire(free_slot);
}
