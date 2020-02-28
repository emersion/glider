#define _XOPEN_SOURCE 700
#include <assert.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <stdlib.h>
#include <strings.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/backend.h"

static bool get_drm_resources(struct glider_drm_device *device) {
	drmModeRes *res = drmModeGetResources(device->fd);
	if (res == NULL) {
		wlr_log_errno(WLR_ERROR, "drmModeGetResources failed");
		return false;
	}

	device->crtcs = calloc(res->count_crtcs, sizeof(struct glider_drm_crtc));
	if (device->crtcs == NULL) {
		goto error_crtc;
	}

	for (int i = 0; i < res->count_crtcs; i++) {
		if (!init_drm_crtc(&device->crtcs[i], device, res->crtcs[i])) {
			goto error_crtc;
		}
		device->crtcs_len++;
	}

	drmModePlaneRes *plane_res = drmModeGetPlaneResources(device->fd);
	if (plane_res == NULL) {
		goto error_crtc;
	}

	device->planes =
		calloc(plane_res->count_planes, sizeof(struct glider_drm_plane));
	if (device->planes == NULL) {
		goto error_plane;
	}

	for (size_t i = 0; i < plane_res->count_planes; i++) {
		if (!init_drm_plane(&device->planes[i], device, plane_res->planes[i])) {
			goto error_plane;
		}
		device->planes_len++;
	}

	// Populate CRTCs primary plane, used to allocate the composition buffer
	for (size_t i = 0; i < device->planes_len; i++) {
		struct glider_drm_plane *plane = &device->planes[i];
		if (plane->props[GLIDER_DRM_PLANE_TYPE].current !=
				DRM_PLANE_TYPE_PRIMARY) {
			continue;
		}

		for (size_t j = 0; j < device->crtcs_len; j++) {
			struct glider_drm_crtc *crtc = &device->crtcs[j];
			if (plane->plane->possible_crtcs & (1 << j) &&
					crtc->primary_plane == NULL) {
				crtc->primary_plane = plane;
				break;
			}
		}
	}
	for (size_t i = 0; i < device->crtcs_len; i++) {
		assert(device->crtcs[i].primary_plane != NULL);
	}

	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);
	return true;

error_plane:
	for (size_t i = 0; i < device->planes_len; i++) {
		finish_drm_plane(&device->planes[i]);
	}
	drmModeFreePlaneResources(plane_res);
error_crtc:
	for (size_t i = 0; i < device->crtcs_len; i++) {
		finish_drm_crtc(&device->crtcs[i]);
	}
	return false;
}

static void handle_invalidated(struct wl_listener *listener, void *data) {
	struct glider_drm_device *device =
		wl_container_of(listener, device, invalidated);
	refresh_drm_device(device);
}

static struct glider_drm_crtc *crtc_from_id(struct glider_drm_device *device,
		uint32_t crtc_id) {
	for (size_t i = 0; i < device->crtcs_len; i++) {
		struct glider_drm_crtc *crtc = &device->crtcs[i];
		if (crtc->id == crtc_id) {
			return crtc;
		}
	}
	return NULL;
}

static void handle_page_flip(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct glider_drm_device *device = data;

	struct glider_drm_crtc *crtc = crtc_from_id(device, crtc_id);
	if (crtc == NULL) {
		wlr_log(WLR_ERROR, "Received page-flip for unknown CRTC %"PRIu32,
			crtc_id);
		return;
	}

	struct timespec t = {
		.tv_sec = tv_sec,
		.tv_nsec = tv_usec * 1000,
	};

	handle_drm_crtc_page_flip(crtc, seq, &t);
}

static int handle_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = 3,
		.page_flip_handler2 = handle_page_flip,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

// TODO: move to wlroots
static void format_set_union(struct wlr_drm_format_set *dst,
		const struct wlr_drm_format_set *src) {
	for (size_t i = 0; i < src->len; i++) {
		const struct wlr_drm_format *fmt = src->formats[i];
		wlr_drm_format_set_add(dst, fmt->format, DRM_FORMAT_MOD_INVALID);
		for (size_t j = 0; j < fmt->len; j++) {
			wlr_drm_format_set_add(dst, fmt->format, fmt->modifiers[j]);
		}
	}
}

bool init_drm_device(struct glider_drm_device *device,
		struct glider_drm_backend *backend, int fd) {
	device->backend = backend;
	device->fd = fd;

	wl_list_init(&device->connectors);
	wl_list_init(&device->buffers);
	wl_list_init(&device->invalidated.link);

	if (drmSetClientCap(device->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		wlr_log(WLR_ERROR, "DRM_CLIENT_CAP_UNIVERSAL_PLANES unsupported");
		return false;
	}
	if (drmSetClientCap(device->fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		wlr_log(WLR_ERROR, "DRM_CLIENT_CAP_ATOMIC unsupported");
		return false;
	}

	uint64_t cap;
	if (drmGetCap(device->fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) != 0 ||
			!cap) {
		wlr_log(WLR_ERROR, "DRM_CRTC_IN_VBLANK_EVENT unsupported");
		return false;
	}

	int ret = drmGetCap(device->fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
	device->cap_addfb2_modifiers = ret == 0 && cap == 1;

	device->gbm = gbm_create_device(device->fd);
	if (device->gbm == NULL) {
		return false;
	}

	device->liftoff_device = liftoff_device_create(device->fd);
	if (device->liftoff_device == NULL) {
		goto error_gbm;
	}

	if (!get_drm_resources(device)) {
		goto error_liftoff;
	}

	for (size_t i = 0; i < device->planes_len; i++) {
		format_set_union(&device->formats, &device->planes[i].formats);
	}

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(backend->display);
	device->event_source = wl_event_loop_add_fd(event_loop, device->fd,
		WL_EVENT_READABLE, handle_drm_event, NULL);
	if (device->event_source == NULL) {
		wlr_log(WLR_ERROR, "wl_event_loop_add_fd failed");
		finish_drm_device(device);
		return false;
	}

	device->invalidated.notify = handle_invalidated;
	wlr_session_signal_add(backend->session, fd, &device->invalidated);

	return true;

error_liftoff:
	liftoff_device_destroy(device->liftoff_device);
error_gbm:
	gbm_device_destroy(device->gbm);
	return false;
}

static void destroy_drm_buffer(struct glider_drm_buffer *drm_buffer);

void finish_drm_device(struct glider_drm_device *device) {
	struct glider_drm_buffer *buf, *buf_tmp;
	wl_list_for_each_safe(buf, buf_tmp, &device->buffers, link) {
		destroy_drm_buffer(buf);
	}

	struct glider_drm_connector *conn, *conn_tmp;
	wl_list_for_each_safe(conn, conn_tmp, &device->connectors, link) {
		destroy_drm_connector(conn);
	}

	if (device->event_source != NULL) {
		wl_event_source_remove(device->event_source);
	}

	wl_list_remove(&device->invalidated.link);
	for (size_t i = 0; i < device->planes_len; i++) {
		finish_drm_plane(&device->planes[i]);
	}
	free(device->planes);
	for (size_t i = 0; i < device->crtcs_len; i++) {
		finish_drm_crtc(&device->crtcs[i]);
	}
	free(device->crtcs);
	liftoff_device_destroy(device->liftoff_device);
	wlr_drm_format_set_finish(&device->formats);
	gbm_device_destroy(device->gbm);
	wlr_session_close_file(device->backend->session, device->fd);
}

static ssize_t get_conn_index(drmModeRes *res, uint32_t id) {
	for (int i = 0; i < res->count_connectors; i++) {
		if (res->connectors[i] == id) {
			return i;
		}
	}
	return -1;
}

bool refresh_drm_device(struct glider_drm_device *device) {
	wlr_log(WLR_DEBUG, "Refreshing DRM device");

	drmModeRes *res = drmModeGetResources(device->fd);
	if (res == NULL) {
		wlr_log_errno(WLR_ERROR, "drmModeGetResources failed");
		return false;
	}

	bool seen[res->count_connectors + 1];
	memset(seen, 0, res->count_connectors * sizeof(bool));

	struct glider_drm_connector *conn, *conn_tmp;
	wl_list_for_each_safe(conn, conn_tmp, &device->connectors, link) {
		ssize_t index = get_conn_index(res, conn->id);
		if (index < 0) {
			wlr_log(WLR_DEBUG, "Connector %"PRIu32" disappeared", conn->id);
			destroy_drm_connector(conn);
			continue;
		}

		if (!refresh_drm_connector(conn)) {
			goto error;
		}

		seen[index] = true;
	}

	for (int i = 0; i < res->count_connectors; i++) {
		if (seen[i]) {
			continue;
		}

		wlr_log(WLR_DEBUG, "Connector %"PRIu32" appeared", res->connectors[i]);
		struct glider_drm_connector *conn =
			create_drm_connector(device, res->connectors[i]);
		if (conn == NULL) {
			goto error;
		}
		if (!refresh_drm_connector(conn)) {
			goto error;
		}
	}

	drmModeFreeResources(res);
	return true;

error:
	drmModeFreeResources(res);
	return false;
}

static struct gbm_bo *import_dmabuf(struct glider_drm_device *device,
		struct wlr_dmabuf_attributes *dmabuf) {
	uint32_t usage = GBM_BO_USE_SCANOUT;

	if (dmabuf->n_planes > GBM_MAX_PLANES) {
		wlr_log(WLR_ERROR, "DMA-BUF contains too many planes (%d)",
			dmabuf->n_planes);
		return NULL;
	}

	struct gbm_bo *bo;
	if (dmabuf->modifier != DRM_FORMAT_MOD_INVALID || dmabuf->n_planes > 1 ||
			dmabuf->offset[0] > 0) {
		struct gbm_import_fd_modifier_data import_mod = {
			.width = dmabuf->width,
			.height = dmabuf->height,
			.format = dmabuf->format,
			.modifier = dmabuf->modifier,
			.num_fds = dmabuf->n_planes,
		};
		memcpy(import_mod.fds, dmabuf->fd,
			sizeof(dmabuf->fd[0]) * dmabuf->n_planes);
		memcpy(import_mod.strides, dmabuf->stride,
			sizeof(dmabuf->stride[0]) * dmabuf->n_planes);
		memcpy(import_mod.offsets, dmabuf->offset,
			sizeof(dmabuf->offset[0]) * dmabuf->n_planes);
		bo = gbm_bo_import(device->gbm, GBM_BO_IMPORT_FD_MODIFIER, &import_mod,
			usage);
	} else {
		struct gbm_import_fd_data import = {
			.width = dmabuf->width,
			.height = dmabuf->height,
			.stride = dmabuf->stride[0],
			.format = dmabuf->format,
			.fd = dmabuf->fd[0],
		};
		bo = gbm_bo_import(device->gbm, GBM_BO_IMPORT_FD, &import, usage);
	}
	if (bo == NULL) {
		wlr_log(WLR_ERROR, "gbm_bo_import failed");
	}
	return bo;
}

static uint32_t add_gbm_bo(struct glider_drm_device *device,
		struct gbm_bo *bo) {
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);
	uint64_t modifier = gbm_bo_get_modifier(bo);

	uint32_t handles[4] = {0};
	uint64_t modifiers[4] = {0};
	uint32_t strides[4] = {0};
	uint32_t offsets[4] = {0};
	for (int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		// KMS requires all BO planes to have the same modifier
		modifiers[i] = modifier;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
	}

	uint32_t fb_id = 0;
	if (device->cap_addfb2_modifiers && modifier != DRM_FORMAT_MOD_INVALID) {
		if (drmModeAddFB2WithModifiers(device->fd, width, height, format,
				handles, strides, offsets, modifiers, &fb_id,
				DRM_MODE_FB_MODIFIERS) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeAddFB2WithModifiers failed");
			return 0;
		}
	} else {
		if (drmModeAddFB2(device->fd, width, height, format, handles, strides,
				offsets, &fb_id, 0) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeAddFB2 failed");
			return 0;
		}
	}

	return fb_id;
}

static void handle_buffer_destroy(struct wl_listener *listener, void *data) {
	struct glider_drm_buffer *drm_buffer =
		wl_container_of(listener, drm_buffer, destroy);
	destroy_drm_buffer(drm_buffer);
}

struct glider_drm_buffer *get_or_create_drm_buffer(
		struct glider_drm_device *device, struct wlr_buffer *buffer) {
	struct glider_drm_buffer *drm_buffer;
	wl_list_for_each(drm_buffer, &device->buffers, link) {
		if (drm_buffer->buffer == buffer) {
			return drm_buffer;
		}
	}

	struct wlr_dmabuf_attributes dmabuf;
	if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
		return NULL;
	}

	if (!wlr_drm_format_set_has(&device->formats,
			dmabuf.format, dmabuf.modifier)) {
		wlr_log(WLR_DEBUG, "No plane can scan-out format 0x%"PRIX32", "
			"modifier 0x%"PRIX64, dmabuf.format, dmabuf.modifier);
		return NULL;
	}

	drm_buffer = calloc(1, sizeof(*drm_buffer));
	if (drm_buffer == NULL) {
		return NULL;
	}

	drm_buffer->buffer = buffer;
	drm_buffer->device = device;

	// In theory we could bypass GBM and directly add the FB via some
	// drmPrimeFDToHandle calls, however this leads to various issues regarding
	// GEM handles and usage
	drm_buffer->gbm = import_dmabuf(device, &dmabuf);
	if (drm_buffer->gbm == NULL) {
		goto error_drm_buffer;
	}

	drm_buffer->id = add_gbm_bo(device, drm_buffer->gbm);
	if (drm_buffer->id == 0) {
		goto error_gbm;
	}

	drm_buffer->destroy.notify = handle_buffer_destroy;
	wl_signal_add(&buffer->events.destroy, &drm_buffer->destroy);

	wl_list_insert(&device->buffers, &drm_buffer->link);

	return drm_buffer;

error_gbm:
	gbm_bo_destroy(drm_buffer->gbm);
error_drm_buffer:
	free(drm_buffer);
	return NULL;
}

static void destroy_drm_buffer(struct glider_drm_buffer *buffer) {
	if (drmModeRmFB(buffer->device->fd, buffer->id) != 0) {
		wlr_log_errno(WLR_ERROR, "drmModeRmFB failed");
	}
	gbm_bo_destroy(buffer->gbm);
	for (size_t i = 0; i < buffer->device->crtcs_len; i++) {
		struct glider_drm_crtc *crtc = &buffer->device->crtcs[i];
		for (size_t j = 0; j < crtc->attachments_cap; j++) {
			struct glider_drm_attachment *att = &crtc->attachments[j];
			if (att->state != GLIDER_DRM_BUFFER_UNLOCKED &&
					att->buffer == buffer) {
				unlock_drm_attachment(att);
			}
		}
	}
	wl_list_remove(&buffer->destroy.link);
	wl_list_remove(&buffer->link);
	free(buffer);
}
