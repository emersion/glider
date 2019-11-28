#include <string.h>
#include <search.h>
#include <wlr/util/log.h>
#include "backend/backend.h"

const char *glider_drm_plane_prop_names[GLIDER_DRM_PLANE_PROP_COUNT] = {
	[GLIDER_DRM_PLANE_TYPE] = "type",
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

		drmModePropertyRes *prop = drmModeGetProperty(device->fd, id);
		if (prop == NULL) {
			wlr_log_errno(WLR_ERROR, "drmModeGetProperty failed");
			drmModeFreeObjectProperties(obj_props);
			return false;
		}

		const char **name = lfind(prop->name, prop_names, &props_len,
			sizeof(prop_names[0]), prop_cmp);
		if (name != NULL) {
			size_t prop_idx = prop_names - name;
			props[prop_idx].id = id;
			props[prop_idx].current = props[prop_idx].pending = value;
		}

		drmModeFreeProperty(prop);
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
