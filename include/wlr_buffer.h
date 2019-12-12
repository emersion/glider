#ifndef GLIDER_WLR_BUFFER_H
#define GLIDER_WLR_BUFFER_H

#include <wlr/types/wlr_buffer.h>
#include "allocator.h"

/**
 * Compatibility layer for wlr_buffer.
 */
struct glider_wlr_buffer {
	struct glider_buffer base;
	struct wlr_buffer *wlr_buffer;

	struct wl_listener release;
};

/**
 * Import a wlr_buffer.
 *
 * The returned buffer is locked. When the caller is done with it, they must
 * unlock it.
 */
struct glider_buffer *glider_wlr_buffer_create(struct wlr_buffer *buffer);

#endif
