#include <drm_fourcc.h>
#include "backend/backend.h"

bool init_drm_plane(struct glider_drm_plane *plane,
		struct glider_drm_device *device, uint32_t id) {
	plane->device = device;
	plane->id = id;

	plane->plane = drmModeGetPlane(device->fd, id);
	if (plane->plane == NULL) {
		return false;
	}

	if (!init_drm_props(plane->props, glider_drm_plane_props,
			GLIDER_DRM_PLANE_PROP_COUNT, device, id, DRM_MODE_OBJECT_PLANE)) {
		drmModeFreePlane(plane->plane);
		return false;
	}

	for (size_t i = 0; i < plane->plane->count_formats; i++) {
		wlr_drm_format_set_add(&plane->formats, plane->plane->formats[i],
			DRM_FORMAT_MOD_INVALID);
	}
	// TODO: IN_FORMATS prop

	return true;
}

void finish_drm_plane(struct glider_drm_plane *plane) {
	wlr_drm_format_set_finish(&plane->formats);
	drmModeFreePlane(plane->plane);
}
