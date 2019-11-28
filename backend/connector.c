#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "backend/backend.h"

static const struct wlr_output_impl output_impl;

static struct glider_drm_connector *get_drm_connector_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output->impl == &output_impl);
	return (struct glider_drm_connector *)wlr_output;
}

static bool output_attach_render(struct wlr_output *output, int *buffer_age) {
	return false;
}

static bool output_commit(struct wlr_output *output) {
	return false;
}

static void output_destroy(struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	for (size_t i = 0; i < conn->modes_len; i++) {
		wl_list_remove(&conn->modes[i].wlr_mode.link);
	}
	free(conn->modes);
	conn->modes = NULL;

	memset(&conn->output, 0, sizeof(struct wlr_output));
}

static const struct wlr_output_impl output_impl = {
	.attach_render = output_attach_render,
	.commit = output_commit,
	.destroy = output_destroy,
};

static uint32_t get_possible_crtcs(int drm_fd, drmModeConnector *conn) {
	uint32_t ret = 0;

	for (int i = 0; i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoders[i]);
		if (enc == NULL) {
			wlr_log_errno(WLR_ERROR, "drmModeGetEncoder failed");
			return 0;
		}
		ret |= enc->possible_crtcs;
		drmModeFreeEncoder(enc);
	}

	// TODO: sometimes DP MST connectors report no encoders
	return ret;
}

struct glider_drm_connector *create_drm_connector(
		struct glider_drm_device *device, uint32_t id) {
	struct glider_drm_connector *conn = calloc(1, sizeof(*conn));
	if (conn == NULL) {
		return NULL;
	}

	conn->device = device;
	conn->id = id;

	// Populate the connector with properties that don't change across hotplugs
	drmModeConnector *drm_conn = drmModeGetConnectorCurrent(device->fd, id);
	if (drm_conn == NULL) {
		free(conn);
		return false;
	}

	conn->possible_crtcs = get_possible_crtcs(device->fd, drm_conn);

	drmModeFreeConnector(drm_conn);
	wl_list_insert(&device->connectors, &conn->link);
	return conn;
}

void destroy_drm_connector(struct glider_drm_connector *conn) {
	if (conn->connection == DRM_MODE_CONNECTED) {
		wlr_output_destroy(&conn->output);
	}
	wl_list_remove(&conn->link);
	free(conn);
}

int32_t calculate_refresh_rate(const drmModeModeInfo *mode) {
	int32_t refresh = (mode->clock * 1000000LL / mode->htotal +
		mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		refresh *= 2;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		refresh /= 2;
	}

	if (mode->vscan > 1) {
		refresh /= mode->vscan;
	}

	return refresh;
}

static bool update_modes(struct glider_drm_connector *conn,
		const drmModeModeInfo *drm_modes, size_t modes_len) {
	assert(conn->modes == NULL);
	conn->modes = calloc(modes_len, sizeof(struct glider_drm_mode));
	if (conn->modes == NULL) {
		return false;
	}

	for (size_t i = 0; i < modes_len; i++) {
		struct glider_drm_mode *mode = &conn->modes[i];

		if (drm_modes[i].flags & DRM_MODE_FLAG_INTERLACE) {
			continue;
		}

		mode->drm_mode = drm_modes[i];
		mode->wlr_mode.width = mode->drm_mode.hdisplay;
		mode->wlr_mode.height = mode->drm_mode.vdisplay;
		mode->wlr_mode.refresh = calculate_refresh_rate(&mode->drm_mode);
		if (mode->drm_mode.type & DRM_MODE_TYPE_PREFERRED) {
			mode->wlr_mode.preferred = true;
		}

		wl_list_insert(&conn->output.modes, &mode->wlr_mode.link);
		conn->modes_len++;
	}

	return true;
}

bool refresh_drm_connector(struct glider_drm_connector *conn) {
	drmModeConnector *drm_conn =
		drmModeGetConnector(conn->device->fd, conn->id);
	if (drm_conn == NULL) {
		wlr_log_errno(WLR_ERROR, "drmModeGetConnector failed");
		return false;
	}

	if (conn->connection == DRM_MODE_CONNECTED &&
			drm_conn->connection != DRM_MODE_CONNECTED) {
		wlr_log(WLR_DEBUG, "Connector %"PRIu32" disconnected", conn->id);
		wlr_output_destroy(&conn->output);
	}

	if (conn->connection != DRM_MODE_CONNECTED &&
			drm_conn->connection == DRM_MODE_CONNECTED) {
		wlr_log(WLR_DEBUG, "Connector %"PRIu32" connected", conn->id);

		struct glider_drm_backend *backend = conn->device->backend;
		wlr_output_init(&conn->output, &backend->base, &output_impl,
			backend->display);

		// TODO: always refresh modes, remove and re-add output if they changed
		if (!update_modes(conn, drm_conn->modes, drm_conn->count_modes)) {
			return false;
		}

		wl_signal_emit(&backend->base.events.new_output,
			&conn->output);
	}

	conn->connection = drm_conn->connection;

	drmModeFreeConnector(drm_conn);
	return true;
}
