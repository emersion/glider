#include <stdlib.h>
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
	drmModeFreeResources(res);
	return false;
}

static void handle_invalidated(struct wl_listener *listener, void *data) {
	struct glider_drm_device *device =
		wl_container_of(listener, device, invalidated);
	// TODO
}

bool init_drm_device(struct glider_drm_device *device,
		struct glider_drm_backend *backend, int fd) {
	device->backend = backend;
	device->fd = fd;

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
	wl_list_remove(&device->invalidated.link);
	for (size_t i = 0; i < device->crtcs_len; i++) {
		finish_drm_crtc(&device->crtcs[i]);
	}
	liftoff_device_destroy(device->liftoff_device);
	wlr_session_close_file(device->backend->session, device->fd);
}
