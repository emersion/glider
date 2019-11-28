#include "backend/backend.h"

bool init_drm_plane(struct glider_drm_plane *plane,
		struct glider_drm_device *device, uint32_t id) {
	plane->device = device;
	plane->id = id;
	return true;
}

void finish_drm_plane(struct glider_drm_plane *plane) {

}
