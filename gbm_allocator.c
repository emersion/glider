#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "gbm_allocator.h"

static const struct wlr_buffer_impl buffer_impl;

static struct glider_gbm_buffer *get_gbm_buffer_from_buffer(
		struct wlr_buffer *buffer) {
	assert(buffer->impl == &buffer_impl);
	return (struct glider_gbm_buffer *)buffer;
}

struct glider_gbm_buffer *glider_gbm_buffer_create(struct gbm_device *gbm_device,
		int width, int height, const struct wlr_drm_format *format) {
	struct gbm_bo *bo = NULL;
	if (format->len > 0) {
		bo = gbm_bo_create_with_modifiers(gbm_device, width, height,
			format->format, format->modifiers, format->len);
	}
	if (bo == NULL) {
		uint32_t usage = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
		if (format->len == 1 &&
				format->modifiers[0] == DRM_FORMAT_MOD_LINEAR) {
			usage |= GBM_BO_USE_LINEAR;
		}
		bo = gbm_bo_create(gbm_device, width, height, format->format, usage);
	}
	if (bo == NULL) {
		wlr_log(WLR_ERROR, "gbm_bo_create failed");
		return NULL;
	}

	struct glider_gbm_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		gbm_bo_destroy(bo);
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);
	buffer->gbm_bo = bo;

	wlr_log(WLR_DEBUG, "Allocated %dx%d GBM buffer (format 0x%"PRIX32", "
		"modifier 0x%"PRIX64")", buffer->base.width, buffer->base.height,
		gbm_bo_get_format(bo), gbm_bo_get_modifier(bo));

	return buffer;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct glider_gbm_buffer *buffer =
		get_gbm_buffer_from_buffer(wlr_buffer);
	gbm_bo_destroy(buffer->gbm_bo);
	free(buffer);
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	struct glider_gbm_buffer *buffer =
		get_gbm_buffer_from_buffer(wlr_buffer);

	memset(attribs, 0, sizeof(struct wlr_dmabuf_attributes));

	struct gbm_bo *bo = buffer->gbm_bo;

	attribs->n_planes = gbm_bo_get_plane_count(bo);
	if (attribs->n_planes > WLR_DMABUF_MAX_PLANES) {
		wlr_log(WLR_ERROR, "GBM BO contains too many planes (%d)",
			attribs->n_planes);
		return false;
	}

	attribs->width = gbm_bo_get_width(bo);
	attribs->height = gbm_bo_get_height(bo);
	attribs->format = gbm_bo_get_format(bo);
	attribs->modifier = gbm_bo_get_modifier(bo);

	int i;
	for (i = 0; i < attribs->n_planes; ++i) {
		attribs->offset[i] = gbm_bo_get_offset(bo, i);
		attribs->stride[i] = gbm_bo_get_stride_for_plane(bo, i);
		attribs->fd[i] = gbm_bo_get_fd(bo);
		if (attribs->fd[i] < 0) {
			wlr_log(WLR_ERROR, "gbm_bo_get_fd failed");
			goto error_fd;
		}
	}

	return true;

error_fd:
	for (int j = 0; j < i; ++j) {
		close(attribs->fd[j]);
	}
	return false;
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};

static const struct glider_allocator_interface allocator_impl;

static struct glider_gbm_allocator *get_gbm_alloc_from_alloc(
		struct glider_allocator *alloc) {
	assert(alloc->impl == &allocator_impl);
	return (struct glider_gbm_allocator *)alloc;
}

struct glider_gbm_allocator *glider_gbm_allocator_create(int fd) {
	struct glider_gbm_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	glider_allocator_init(&alloc->base, &allocator_impl);

	alloc->fd = fd;

	alloc->gbm_device = gbm_create_device(fd);
	if (alloc->gbm_device == NULL) {
		wlr_log(WLR_ERROR, "gbm_create_device failed");
		free(alloc);
		return NULL;
	}

	return alloc;
}

static void allocator_destroy(struct glider_allocator *glider_alloc) {
	struct glider_gbm_allocator *alloc = get_gbm_alloc_from_alloc(glider_alloc);
	gbm_device_destroy(alloc->gbm_device);
	close(alloc->fd);
	free(alloc);
}

static struct wlr_buffer *allocator_create_buffer(
		struct glider_allocator *glider_alloc, int width, int height,
		const struct wlr_drm_format *format) {
	struct glider_gbm_allocator *alloc = get_gbm_alloc_from_alloc(glider_alloc);
	struct glider_gbm_buffer *buffer =
		glider_gbm_buffer_create(alloc->gbm_device, width, height, format);
	if (buffer == NULL) {
		return NULL;
	}
	return &buffer->base;
}

static const struct glider_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};
