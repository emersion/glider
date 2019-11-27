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
		drmModeFreeResources(res);
		return false;
	}

	for (int i = 0; i < res->count_crtcs; i++) {
		if (!init_drm_crtc(&device->crtcs[i], device, res->crtcs[i])) {
			goto error_crtc;
		}
		device->crtcs_len++;
	}

	drmModeFreeResources(res);
	return true;

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
	liftoff_device_destroy(device->liftoff_device);
	wlr_session_close_file(device->backend->session, device->fd);
}
