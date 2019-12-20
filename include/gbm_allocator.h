#ifndef GLIDER_GBM_ALLOCATOR_H
#define GLIDER_GBM_ALLOCATOR_H

#include <gbm.h>
#include "allocator.h"

struct glider_gbm_buffer {
	struct glider_buffer base;

	struct gbm_bo *gbm_bo;
};

struct glider_gbm_allocator {
	struct glider_allocator base;

	struct gbm_device *gbm_device;
};

struct glider_gbm_buffer *glider_gbm_buffer_create(struct gbm_device *gbm_device,
	int width, int height, const struct wlr_drm_format *format);

struct glider_gbm_allocator *glider_gbm_allocator_create(int render_fd);

#endif
