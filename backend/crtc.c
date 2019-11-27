#include <stdlib.h>
#include "backend/backend.h"

bool init_drm_crtc(struct glider_drm_crtc *crtc,
		struct glider_drm_device *device, uint32_t id) {
	crtc->device = device;
	crtc->id = id;
	return true;
}

void finish_drm_crtc(struct glider_drm_crtc *crtc) {
}
