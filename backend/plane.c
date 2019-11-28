#include "backend/backend.h"

bool init_drm_plane(struct glider_drm_plane *plane,
		struct glider_drm_device *device, uint32_t id) {
	plane->device = device;
	plane->id = id;

	if (!init_drm_props(plane->props, glider_drm_plane_prop_names,
			GLIDER_DRM_PLANE_PROP_COUNT, device, id, DRM_MODE_OBJECT_PLANE)) {
		return false;
	}

	return true;
}

void finish_drm_plane(struct glider_drm_plane *plane) {

}
