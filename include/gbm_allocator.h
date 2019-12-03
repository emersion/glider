#ifndef GLIDER_GBM_ALLOCATOR_H
#define GLIDER_GBM_ALLOCATOR_H

#include "allocator.h"

struct glider_gbm_buffer {
	struct glider_buffer base;

	struct gbm_bo *gbm_bo;
};

struct glider_buffer *glider_gbm_buffer_create(struct gbm_device *gbm_device,
	int width, int height, const struct wlr_drm_format *format);

#endif
