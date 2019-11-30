#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "allocator.h"

struct glider_allocator *glider_gbm_allocator_create(int fd) {
	struct glider_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}

	alloc->gbm_device = gbm_create_device(fd);
	if (alloc->gbm_device == NULL) {
		wlr_log(WLR_ERROR, "gbm_create_device failed");
		free(alloc);
		return NULL;
	}

	return alloc;
}

void glider_allocator_destroy(struct glider_allocator *alloc) {
	gbm_device_destroy(alloc->gbm_device);
	free(alloc);
}

struct glider_buffer *glider_allocator_create_buffer(
		struct glider_allocator *alloc, int width, int height, uint32_t format,
		const uint64_t *modifiers, size_t modifiers_len) {
	// TODO: add support for modifiers
	struct gbm_bo *bo = gbm_bo_create(alloc->gbm_device, width, height, format,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (bo == NULL) {
		wlr_log(WLR_ERROR, "gbm_bo_create failed");
		return NULL;
	}

	struct glider_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		gbm_bo_destroy(bo);
		return NULL;
	}
	buffer->gbm_bo = bo;
	buffer->width = width;
	buffer->height = height;
	buffer->format = format;
	buffer->modifier = DRM_FORMAT_MOD_INVALID;
	wl_signal_init(&buffer->events.release);
	return buffer;
}

void glider_buffer_destroy(struct glider_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	if (buffer->dmabuf_attribs.n_planes > 0) {
		wlr_dmabuf_attributes_finish(&buffer->dmabuf_attribs);
	}
	gbm_bo_destroy(buffer->gbm_bo);
	free(buffer);
}

static bool buffer_get_dmabuf(struct glider_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs) {
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

bool glider_buffer_get_dmabuf(struct glider_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs) {
	if (buffer->dmabuf_attribs.n_planes == 0) {
		if (!buffer_get_dmabuf(buffer, &buffer->dmabuf_attribs)) {
			return false;
		}
	}

	memcpy(attribs, &buffer->dmabuf_attribs,
		sizeof(struct wlr_dmabuf_attributes));
	return true;
}
