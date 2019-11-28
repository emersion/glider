#include "backend/backend.h"

bool init_drm_connector(struct glider_drm_connector *conn,
		struct glider_drm_device *device, uint32_t id) {
	conn->device = device;
	conn->id = id;
	return true;
}

void finish_drm_connector(struct glider_drm_connector *conn) {
}
