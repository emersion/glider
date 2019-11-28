#include <stdlib.h>
#include "backend/backend.h"

bool init_drm_crtc(struct glider_drm_crtc *crtc,
		struct glider_drm_device *device, uint32_t id) {
	crtc->device = device;
	crtc->id = id;

	if (!init_drm_props(crtc->props, glider_drm_crtc_props,
			GLIDER_DRM_CRTC_PROP_COUNT, device, id, DRM_MODE_OBJECT_CRTC)) {
		return false;
	}

	crtc->crtc = drmModeGetCrtc(device->fd, id);
	if (crtc->crtc == NULL) {
		return false;
	}

	crtc->liftoff_output = liftoff_output_create(device->liftoff_device, id);
	if (crtc->liftoff_output == NULL) {
		drmModeFreeCrtc(crtc->crtc);
		return false;
	}

	return true;
}

void finish_drm_crtc(struct glider_drm_crtc *crtc) {
	liftoff_output_destroy(crtc->liftoff_output);
	drmModeFreeCrtc(crtc->crtc);
}
