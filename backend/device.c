#define _XOPEN_SOURCE 700
#include <assert.h>
#include <drm_fourcc.h>
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

static struct glider_drm_connector *connector_from_crtc_id(
		struct glider_drm_device *device, uint32_t crtc_id) {
	struct glider_drm_connector *conn;
	wl_list_for_each(conn, &device->connectors, link) {
		if (conn->crtc != NULL && conn->crtc->id == crtc_id) {
			return conn;
		}
	}
	return NULL;
}

static void handle_page_flip(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct glider_drm_device *device = data;

	struct glider_drm_connector *conn = connector_from_crtc_id(device, crtc_id);
	if (conn == NULL) {
		wlr_log(WLR_DEBUG, "Received page-flip for disabled CRTC %"PRIu32,
			crtc_id);
		return;
	}

	struct timespec t = {
		.tv_sec = tv_sec,
		.tv_nsec = tv_usec * 1000,
	};

	handle_drm_connector_page_flip(conn, seq, &t);
}

static int handle_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = 3,
		.page_flip_handler2 = handle_page_flip,
	};

	drmHandleEvent(fd, &event);
	return 1;
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

	device->liftoff_device = liftoff_device_create(device->fd);
	if (device->liftoff_device == NULL) {
		return false;
	}

	if (!get_drm_resources(device)) {
		liftoff_device_destroy(device->liftoff_device);
		return false;
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

static uint32_t import_dmabuf(struct glider_drm_device *device,
		struct wlr_dmabuf_attributes *dmabuf) {
	uint32_t handles[4] = {0};
	uint64_t modifiers[4] = {0};
	for (int i = 0; i < dmabuf->n_planes; i++) {
		if (drmPrimeFDToHandle(device->fd, dmabuf->fd[i], &handles[i]) != 0) {
			wlr_log_errno(WLR_ERROR, "drmPrimeFDToHandle failed");
			return 0;
		}
		// KMS requires all BO planes to have the same modifier
		modifiers[i] = dmabuf->modifier;
	}

	uint32_t fb_id = 0;
	if (device->cap_addfb2_modifiers &&
			dmabuf->modifier != DRM_FORMAT_MOD_INVALID) {
		if (drmModeAddFB2WithModifiers(device->fd, dmabuf->width,
				dmabuf->height, dmabuf->format, handles,
				dmabuf->stride, dmabuf->offset, modifiers, &fb_id,
				DRM_MODE_FB_MODIFIERS) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeAddFB2WithModifiers failed");
			return 0;
		}
	} else {
		if (drmModeAddFB2(device->fd, dmabuf->width, dmabuf->height,
				dmabuf->format, handles, dmabuf->stride, dmabuf->offset,
				&fb_id, 0) != 0) {
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

struct glider_drm_buffer *attach_drm_buffer(struct glider_drm_device *device,
		struct glider_buffer *buffer) {
	struct glider_drm_buffer *drm_buffer;
	wl_list_for_each(drm_buffer, &device->buffers, link) {
		if (drm_buffer->buffer == buffer) {
			return drm_buffer;
		}
	}

	drm_buffer = calloc(1, sizeof(*drm_buffer));
	if (drm_buffer == NULL) {
		return NULL;
	}

	drm_buffer->buffer = buffer;
	drm_buffer->device = device;

	struct wlr_dmabuf_attributes dmabuf;
	if (!glider_buffer_get_dmabuf(buffer, &dmabuf)) {
		free(drm_buffer);
		return 0;
	}

	drm_buffer->id = import_dmabuf(device, &dmabuf);
	if (drm_buffer->id == 0) {
		free(drm_buffer);
		return NULL;
	}

	drm_buffer->destroy.notify = handle_buffer_destroy;
	wl_signal_add(&buffer->events.destroy, &drm_buffer->destroy);

	wl_list_insert(&device->buffers, &drm_buffer->link);

	return drm_buffer;
}

static void destroy_drm_buffer(struct glider_drm_buffer *buffer) {
	if (buffer->locked) {
		unlock_drm_buffer(buffer);
	}
	if (drmModeRmFB(buffer->device->fd, buffer->id) != 0) {
		wlr_log_errno(WLR_ERROR, "drmModeRmFB failed");
	}
	wl_list_remove(&buffer->destroy.link);
	wl_list_remove(&buffer->link);
	free(buffer);
}
