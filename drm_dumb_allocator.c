#include <assert.h>
#include <drm_fourcc.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "drm_dumb_allocator.h"

struct format_info {
	uint32_t format;
	int bpp;
};

static const struct format_info formats[] = {
	{ .format = DRM_FORMAT_XRGB8888, .bpp = 32 },
	{ .format = DRM_FORMAT_ARGB8888, .bpp = 32 },
};

static const struct wlr_buffer_impl buffer_impl;

static struct glider_drm_dumb_buffer *get_drm_dumb_buffer_from_buffer(
		struct wlr_buffer *buffer) {
	assert(buffer->impl == &buffer_impl);
	return (struct glider_drm_dumb_buffer *)buffer;
}

struct glider_drm_dumb_buffer *glider_drm_dumb_buffer_create(int drm_fd,
		int width, int height, uint32_t format) {
	const struct format_info *info = NULL;
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		if (formats[i].format == format) {
			info = &formats[i];
			break;
		}
	}
	if (info == NULL) {
		wlr_log(WLR_ERROR, "unsupported format 0x%"PRIX32, format);
		return NULL;
	}

	struct glider_drm_dumb_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);

	struct drm_mode_create_dumb create = {
		.width = width,
		.height = height,
		.bpp = info->bpp,
		.flags = 0,
	};
	int ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if (ret < 0) {
		wlr_log_errno(WLR_ERROR, "DRM_IOCTL_MODE_CREATE_DUMB failed");
		free(buffer);
		return NULL;
	}

	buffer->drm_fd = drm_fd;
	buffer->width = width;
	buffer->height = height;
	buffer->stride = create.pitch;
	buffer->size = create.size;
	buffer->handle = create.handle;

	wlr_log(WLR_DEBUG, "Allocated %dx%d dumb DRM buffer (format 0x%"PRIX32")",
		buffer->base.width, buffer->base.height, format);

	return buffer;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct glider_drm_dumb_buffer *buffer =
		get_drm_dumb_buffer_from_buffer(wlr_buffer);

	struct drm_mode_destroy_dumb destroy = { .handle = buffer->handle };
	int ret = drmIoctl(buffer->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	if (ret < 0) {
		wlr_log_errno(WLR_ERROR, "DRM_IOCTL_DESTROY_DUMB failed");
	}

	free(buffer);
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	return false;
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};

void *glider_drm_dumb_buffer_map(struct glider_drm_dumb_buffer *buffer) {
	struct drm_mode_map_dumb map = { .handle = buffer->handle };
	int ret = drmIoctl(buffer->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if (ret < 0) {
		wlr_log_errno(WLR_ERROR, "DRM_IOCTL_MODE_MAP_DUMB failed");
		return MAP_FAILED;
	}

	void *data = mmap(0, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		buffer->drm_fd, map.offset);
	if (data == MAP_FAILED) {
		wlr_log_errno(WLR_ERROR, "mmap failed");
	}
	return data;
}

static const struct glider_allocator_interface allocator_impl;

static struct glider_drm_dumb_allocator *get_drm_dumb_alloc_from_alloc(
		struct glider_allocator *alloc) {
	assert(alloc->impl == &allocator_impl);
	return (struct glider_drm_dumb_allocator *)alloc;
}

struct glider_drm_dumb_allocator *glider_drm_dumb_allocator_create(int fd) {
	struct glider_drm_dumb_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	glider_allocator_init(&alloc->base, &allocator_impl);

	alloc->fd = fd;

	return alloc;
}

static void allocator_destroy(struct glider_allocator *glider_alloc) {
	struct glider_drm_dumb_allocator *alloc =
		get_drm_dumb_alloc_from_alloc(glider_alloc);
	free(alloc);
}

static struct wlr_buffer *allocator_create_buffer(
		struct glider_allocator *glider_alloc, int width, int height,
		const struct wlr_drm_format *format) {
	struct glider_drm_dumb_allocator *alloc =
		get_drm_dumb_alloc_from_alloc(glider_alloc);
	struct glider_drm_dumb_buffer *buffer =
		glider_drm_dumb_buffer_create(alloc->fd, width, height, format->format);
	if (buffer == NULL) {
		return NULL;
	}
	return &buffer->base;
}

static const struct glider_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};
