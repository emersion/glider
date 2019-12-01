#include <drm_fourcc.h>
#include <wlr/util/log.h>
#include "backend/backend.h"

bool init_drm_plane(struct glider_drm_plane *plane,
		struct glider_drm_device *device, uint32_t id) {
	plane->device = device;
	plane->id = id;

	plane->plane = drmModeGetPlane(device->fd, id);
	if (plane->plane == NULL) {
		wlr_log_errno(WLR_ERROR, "drmModeGetPlane failed");
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

	if (plane->props[GLIDER_DRM_PLANE_IN_FORMATS].current != 0) {
		drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(device->fd,
			plane->props[GLIDER_DRM_PLANE_IN_FORMATS].current);
		if (blob == NULL) {
			wlr_log_errno(WLR_ERROR, "drmModeGetPropertyBlob failed");
			finish_drm_plane(plane);
			return false;
		}

		struct drm_format_modifier_blob *data = blob->data;
		uint32_t *fmts = (uint32_t *)((char *)data + data->formats_offset);
		struct drm_format_modifier *mods = (struct drm_format_modifier *)
			((char *)data + data->modifiers_offset);
		for (uint32_t i = 0; i < data->count_modifiers; ++i) {
			for (int j = 0; j < 64; ++j) {
				if (mods[i].formats & ((uint64_t)1 << j)) {
					wlr_drm_format_set_add(&plane->formats,
						fmts[j + mods[i].offset], mods[i].modifier);
				}
			}
		}

		drmModeFreePropertyBlob(blob);
	}

	return true;
}

void finish_drm_plane(struct glider_drm_plane *plane) {
	wlr_drm_format_set_finish(&plane->formats);
	drmModeFreePlane(plane->plane);
}
