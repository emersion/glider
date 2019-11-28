#define _XOPEN_SOURCE 700
#include <assert.h>
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

		// There should be exactly one primary plane per CRTC
		int crtc_bit = ffs(plane->plane->possible_crtcs) - 1;

		// This would be a kernel bug
		assert(crtc_bit >= 0 && (size_t)crtc_bit < device->crtcs_len);

		struct glider_drm_crtc *crtc = &device->crtcs[crtc_bit];
		crtc->primary_plane = plane;
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

bool init_drm_device(struct glider_drm_device *device,
		struct glider_drm_backend *backend, int fd) {
	device->backend = backend;
	device->fd = fd;

	wl_list_init(&device->connectors);

	if (drmSetClientCap(device->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		wlr_log(WLR_ERROR, "DRM_CLIENT_CAP_UNIVERSAL_PLANES unsupported");
		return false;
	}
	if (drmSetClientCap(device->fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		wlr_log(WLR_ERROR, "DRM_CLIENT_CAP_ATOMIC unsupported");
		//return false;
	}

	uint64_t cap;
	if (drmGetCap(device->fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) != 0 ||
			!cap) {
		wlr_log(WLR_ERROR, "DRM_CRTC_IN_VBLANK_EVENT unsupported");
		return false;
	}

	device->liftoff_device = liftoff_device_create(device->fd);
	if (device->liftoff_device == NULL) {
		return false;
	}

	if (!get_drm_resources(device)) {
		liftoff_device_destroy(device->liftoff_device);
		return false;
	}

	device->invalidated.notify = handle_invalidated;
	wlr_session_signal_add(backend->session, fd, &device->invalidated);

	return true;
}

void finish_drm_device(struct glider_drm_device *device) {
	struct glider_drm_connector *conn, *conn_tmp;
	wl_list_for_each_safe(conn, conn_tmp, &device->connectors, link) {
		destroy_drm_connector(conn);
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
