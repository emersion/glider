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

struct wlr_buffer *glider_allocator_create_buffer(
		struct glider_allocator *alloc, int width, int height,
		const struct wlr_drm_format *format) {
	return alloc->impl->create_buffer(alloc, width, height, format);
}
