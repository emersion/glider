#define _XOPEN_SOURCE 700
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "backend/backend.h"

static const struct wlr_output_impl output_impl;

static struct glider_drm_connector *get_drm_connector_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output->impl == &output_impl);
	return (struct glider_drm_connector *)wlr_output;
}

static bool connector_commit(struct glider_drm_connector *conn,
		uint32_t flags) {
	if (flags & DRM_MODE_ATOMIC_ALLOW_MODESET) {
		wlr_log(WLR_DEBUG, "Performing atomic modeset on connector %"PRIu32,
			conn->id);
	} else if (flags & DRM_MODE_ATOMIC_TEST_ONLY) {
		wlr_log(WLR_DEBUG, "Performing test-only atomic commit "
			"on connector %"PRIu32, conn->id);
	} else {
		wlr_log(WLR_DEBUG, "Performing atomic commit on connector %"PRIu32,
			conn->id);
	}

	drmModeAtomicReq *req = drmModeAtomicAlloc();
	if (req == NULL) {
		return false;
	}

	if (!apply_drm_props(conn->props, GLIDER_DRM_CONNECTOR_PROP_COUNT,
			conn->id, req)) {
		goto error;
	}
	if (conn->crtc != NULL) {
		if (!apply_drm_props(conn->crtc->props, GLIDER_DRM_CRTC_PROP_COUNT,
				conn->crtc->id, req)) {
			goto error;
		}
		if (!liftoff_output_apply(conn->crtc->liftoff_output, req)) {
			goto error;
		}
	}

	int ret = drmModeAtomicCommit(conn->device->fd, req, flags, conn->device);
	drmModeAtomicFree(req);
	if (ret != 0) {
		wlr_log(WLR_DEBUG, "Atomic commit failed: %s", strerror(-ret));
	}
	if (!(flags & DRM_MODE_ATOMIC_TEST_ONLY)) {
		// Commit properties on success, rollback on failure
		move_drm_prop_values(conn->props,
			GLIDER_DRM_CONNECTOR_PROP_COUNT, ret == 0);
		move_drm_prop_values(conn->crtc->props,
			GLIDER_DRM_CRTC_PROP_COUNT, ret == 0);
	}
	if (!(flags & DRM_MODE_PAGE_FLIP_EVENT) || ret != 0) {
		// Release buffers on test-only commit and on failure: we won't get a
		// page-flip event
		struct glider_drm_buffer *buf;
		wl_list_for_each(buf, &conn->device->buffers, link) {
			if (buf->locked && !buf->presented && buf->connector == conn) {
				unlock_drm_buffer(buf);
			}
		}
	}
	return ret == 0;

error:
	drmModeAtomicFree(req);
	return false;
}

bool glider_drm_connector_commit(struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	return connector_commit(conn, DRM_MODE_PAGE_FLIP_EVENT);
}

bool glider_drm_connector_test(struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	return connector_commit(conn, DRM_MODE_ATOMIC_TEST_ONLY);
}

static struct glider_drm_crtc *connector_pick_crtc(
		struct glider_drm_connector *conn) {
	struct glider_drm_device *device = conn->device;

	// Remove in-use CRTCs from the set
	uint32_t possible_crtcs = conn->possible_crtcs;
	struct glider_drm_connector *c;
	wl_list_for_each(c, &device->connectors, link) {
		if (c->crtc != NULL) {
			size_t crtc_index = c->crtc - device->crtcs;
			possible_crtcs &= ~(1 << crtc_index);
		}
	}
	if (possible_crtcs == 0) {
		return NULL;
	} else {
		int crtc_index = ffs(possible_crtcs) - 1;
		return &device->crtcs[crtc_index];
	}
}

static void connector_set_crtc(struct glider_drm_connector *conn,
		struct glider_drm_crtc *crtc) {
	conn->crtc = crtc;
	conn->props[GLIDER_DRM_CONNECTOR_CRTC_ID].pending = crtc ? crtc->id : 0;
}

static bool output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *wlr_mode) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	struct glider_drm_mode *mode = (struct glider_drm_mode *)wlr_mode;

	struct glider_drm_crtc *crtc = connector_pick_crtc(conn);
	if (crtc == NULL) {
		wlr_log(WLR_ERROR, "Modeset failed: no CRTC available");
		return false;
	}

	uint32_t mode_blob;
	if (drmModeCreatePropertyBlob(conn->device->fd, &mode->drm_mode,
			sizeof(drmModeModeInfo), &mode_blob) != 0) {
		wlr_log_errno(WLR_ERROR, "drmModeCreatePropertyBlob failed");
		return false;
	}

	// TODO: perform a test-only commit
	// TODO: change wlr_output semantics to only apply the mode on commit

	connector_set_crtc(conn, crtc);

	struct glider_drm_prop *mode_id = &crtc->props[GLIDER_DRM_CRTC_MODE_ID];
	if (mode_id->pending != 0 && mode_id->pending != mode_id->initial) {
		if (drmModeDestroyPropertyBlob(conn->device->fd,
				mode_id->pending) != 0) {
			wlr_log_errno(WLR_ERROR, "drmModeDestroyPropertyBlob failed");
			return false;
		}
	}
	mode_id->pending = mode_blob;

	if (!connector_commit(conn, DRM_MODE_ATOMIC_ALLOW_MODESET)) {
		return false;
	}

	wlr_output_update_mode(&conn->output, wlr_mode);
	wlr_output_update_enabled(&conn->output, true);
	return true;
}

static bool output_attach_render(struct wlr_output *output, int *buffer_age) {
	return false;
}

static bool output_commit(struct wlr_output *output) {
	return false;
}

static void output_destroy(struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);

	struct glider_drm_buffer *buf;
	wl_list_for_each(buf, &conn->device->buffers, link) {
		if (buf->locked && buf->connector == conn) {
			unlock_drm_buffer(buf);
		}
	}

	for (size_t i = 0; i < conn->modes_len; i++) {
		wl_list_remove(&conn->modes[i].wlr_mode.link);
	}
	free(conn->modes);
	conn->modes = NULL;
	conn->modes_len = 0;

	memset(&conn->output, 0, sizeof(struct wlr_output));
}

static const struct wlr_output_impl output_impl = {
	.set_mode = output_set_mode,
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

	if (!init_drm_props(conn->props, glider_drm_connector_props,
			GLIDER_DRM_CONNECTOR_PROP_COUNT, device, id,
			DRM_MODE_OBJECT_CONNECTOR)) {
		free(conn);
		return false;
	}

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

const struct wlr_drm_format_set *glider_drm_connector_get_primary_formats(
		struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	if (conn->crtc == NULL) {
		return NULL;
	}
	return &conn->crtc->primary_plane->formats;
}

struct liftoff_output *glider_drm_connector_get_liftoff_output(
		struct wlr_output *output) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	if (conn->crtc == NULL) {
		return NULL;
	}
	return conn->crtc->liftoff_output;
}

bool glider_drm_connector_attach(struct wlr_output *output,
		struct glider_buffer *buffer, struct liftoff_layer *layer) {
	struct glider_drm_connector *conn = get_drm_connector_from_output(output);
	if (conn->crtc == NULL) {
		return false;
	}

	struct glider_drm_buffer *drm_buffer =
		attach_drm_buffer(conn->device, buffer);
	if (drm_buffer == NULL) {
		return false;
	}
	if (drm_buffer->locked) {
		wlr_log(WLR_ERROR, "Cannot attach buffer: buffer is locked");
		return false;
	}

	liftoff_layer_set_property(layer, "FB_ID", drm_buffer->id);
	// TODO: there's no guarantee the layer will be directly scanned out. If
	// that's not the case, do not lock the buffer.
	drm_buffer->locked = true;
	drm_buffer->connector = conn;
	glider_buffer_lock(drm_buffer->buffer);
	return true;
}

static int mhz_to_nsec(int mhz) {
	return 1000000000000LL / mhz;
}

void handle_drm_connector_page_flip(struct glider_drm_connector *conn,
		unsigned seq, struct timespec *t) {
	struct glider_drm_buffer *buf;
	wl_list_for_each(buf, &conn->device->buffers, link) {
		if (buf->locked && buf->connector == conn) {
			if (buf->presented) {
				unlock_drm_buffer(buf);
			} else {
				buf->presented = true;
			}
		}
	}

	uint32_t present_flags = WLR_OUTPUT_PRESENT_VSYNC |
		WLR_OUTPUT_PRESENT_HW_CLOCK | WLR_OUTPUT_PRESENT_HW_COMPLETION;
	// TODO: WLR_OUTPUT_PRESENT_ZERO_COPY
	struct wlr_output_event_present present_event = {
		/* The DRM backend guarantees that the presentation event will be for
		 * the last submitted frame. */
		.commit_seq = conn->output.commit_seq,
		.when = t,
		.seq = seq,
		.refresh = mhz_to_nsec(conn->output.refresh),
		.flags = present_flags,
	};
	wlr_output_send_present(&conn->output, &present_event);

	wlr_output_send_frame(&conn->output);
}

void unlock_drm_buffer(struct glider_drm_buffer *buf) {
	assert(buf->locked);
	buf->locked = buf->presented = false;
	buf->connector = NULL;
	glider_buffer_unlock(buf->buffer);
}
