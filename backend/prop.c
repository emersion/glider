#include <string.h>
#include <search.h>
#include <wlr/util/log.h>
#include "backend/backend.h"

const char *glider_drm_connector_props[GLIDER_DRM_CONNECTOR_PROP_COUNT] = {
	[GLIDER_DRM_CONNECTOR_CRTC_ID] = "CRTC_ID",
};

const char *glider_drm_crtc_props[GLIDER_DRM_CRTC_PROP_COUNT] = {
	[GLIDER_DRM_CRTC_MODE_ID] = "MODE_ID",
};

const char *glider_drm_plane_props[GLIDER_DRM_PLANE_PROP_COUNT] = {
	[GLIDER_DRM_PLANE_TYPE] = "type",
	[GLIDER_DRM_PLANE_IN_FORMATS] = "IN_FORMATS",
};

int prop_cmp(const void *_a, const void *_b) {
	const char *a = _a, **b = (const char **)_b;
	return strcmp(a, *b);
}

bool init_drm_props(struct glider_drm_prop *props, const char **prop_names,
		size_t props_len, struct glider_drm_device *device,
		uint32_t obj_id, uint32_t obj_type) {
	drmModeObjectProperties *obj_props =
		drmModeObjectGetProperties(device->fd, obj_id, obj_type);
	if (obj_props == NULL) {
		wlr_log_errno(WLR_ERROR, "drmModeObjectGetProperties failed");
		return false;
	}

	for (size_t i = 0; i < obj_props->count_props; i++) {
		uint32_t id = obj_props->props[i];
		uint64_t value = obj_props->prop_values[i];

		drmModePropertyRes *drm_prop = drmModeGetProperty(device->fd, id);
		if (drm_prop == NULL) {
			wlr_log_errno(WLR_ERROR, "drmModeGetProperty failed");
			drmModeFreeObjectProperties(obj_props);
			return false;
		}

		const char **name = lfind(drm_prop->name, prop_names, &props_len,
			sizeof(prop_names[0]), prop_cmp);
		if (name != NULL) {
			struct glider_drm_prop *prop = &props[name - prop_names];
			prop->id = id;
			prop->current = prop->pending = prop->initial = value;
		}

		drmModeFreeProperty(drm_prop);
	}

	drmModeFreeObjectProperties(obj_props);

	for (size_t i = 0; i < props_len; i++) {
		if (props[i].id == 0) {
			wlr_log(WLR_ERROR, "Object %"PRIu32" is missing property %s",
				obj_id, prop_names[i]);
			return false;
		}
	}

	return true;
}

bool apply_drm_props(struct glider_drm_prop *props, size_t props_len,
		uint32_t obj_id, drmModeAtomicReq *req) {
	for (size_t i = 0; i < props_len; i++) {
		struct glider_drm_prop *prop = &props[i];
		if (prop->pending == prop->current) {
			continue;
		}
		int ret = drmModeAtomicAddProperty(req, obj_id,
			prop->id, prop->pending);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "drmModeAtomicAddProperty failed");
			return false;
		}
	}
	return true;
}

void move_drm_prop_values(struct glider_drm_prop *props, size_t props_len,
		bool commit) {
	for (size_t i = 0; i < props_len; i++) {
		struct glider_drm_prop *prop = &props[i];
		if (commit) {
			prop->current = prop->pending;
		} else {
			prop->pending = prop->current;
		}
	}
}
