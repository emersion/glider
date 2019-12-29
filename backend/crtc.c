#include <stdlib.h>
#include <wlr/util/log.h>
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
	free(crtc->attachments);
	drmModeFreeCrtc(crtc->crtc);
}

static bool has_queued_buffer(struct glider_drm_crtc *crtc,
		struct liftoff_layer *layer) {
	for (size_t i = 0; i < crtc->attachments_cap; i++) {
		struct glider_drm_attachment *att = &crtc->attachments[i];
		if (att->state == GLIDER_DRM_BUFFER_QUEUED && att->layer == layer) {
			return true;
		}
	}
	return false;
}

void handle_drm_crtc_page_flip(struct glider_drm_crtc *crtc,
		unsigned seq, struct timespec *t) {
	// Release buffers that the new front buffers replaced
	// TODO: doesn't handle liftoff_layer destroy
	for (size_t i = 0; i < crtc->attachments_cap; i++) {
		struct glider_drm_attachment *att = &crtc->attachments[i];
		if (att->state == GLIDER_DRM_BUFFER_CURRENT &&
				has_queued_buffer(crtc, att->layer)) {
			unlock_drm_attachment(att);
		}
	}

	// Mark queued buffers as current
	for (size_t i = 0; i < crtc->attachments_cap; i++) {
		struct glider_drm_attachment *att = &crtc->attachments[i];
		if (att->state == GLIDER_DRM_BUFFER_QUEUED) {
			att->state = GLIDER_DRM_BUFFER_CURRENT;
		}
	}

	struct glider_drm_connector *conn;
	wl_list_for_each(conn, &crtc->device->connectors, link) {
		if (conn->crtc == crtc) {
			handle_drm_connector_page_flip(conn, seq, t);
		}
	}
}

bool set_drm_crtc_mode(struct glider_drm_crtc *crtc,
		const struct glider_drm_mode *mode) {
	uint32_t mode_blob = 0;
	if (mode != NULL) {
		if (drmModeCreatePropertyBlob(crtc->device->fd, &mode->drm_mode,
				sizeof(drmModeModeInfo), &mode_blob) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeCreatePropertyBlob failed");
			return false;
		}
	}

	crtc->props[GLIDER_DRM_CRTC_ACTIVE].pending = mode != NULL;

	struct glider_drm_prop *mode_id = &crtc->props[GLIDER_DRM_CRTC_MODE_ID];
	if (mode_id->pending != 0 && mode_id->pending != mode_id->initial) {
		if (drmModeDestroyPropertyBlob(crtc->device->fd,
				mode_id->pending) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeDestroyPropertyBlob failed");
			return false;
		}
	}
	mode_id->pending = mode_blob;

	return true;
}
