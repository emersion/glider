#ifndef GLIDER_DRM_DUMB_ALLOCATOR_H
#define GLIDER_DRM_DUMB_ALLOCATOR_H

#include <wlr/types/wlr_buffer.h>
#include "allocator.h"

struct glider_drm_dumb_buffer {
	struct wlr_buffer base;

	int drm_fd;
	uint32_t format;
	uint32_t width, height, stride, size;
	uint32_t handle;
};

struct glider_drm_dumb_allocator {
	struct glider_allocator base;

	int fd;
};

struct glider_drm_dumb_allocator *glider_drm_dumb_allocator_create(int drm_fd);

struct glider_drm_dumb_buffer *glider_drm_dumb_buffer_create(int drm_fd,
	int width, int height, uint32_t format);
void *glider_drm_dumb_buffer_map(struct glider_drm_dumb_buffer *buffer);

#endif
