#ifndef GLIDER_BACKEND_BACKEND_H
#define GLIDER_BACKEND_BACKEND_H

#include <libliftoff.h>
#include <time.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/drm_format_set.h>
#include <xf86drmMode.h>
#include "allocator.h"

struct glider_drm_backend;
struct glider_drm_device;

enum glider_drm_connector_prop {
	GLIDER_DRM_CONNECTOR_CRTC_ID,
	GLIDER_DRM_CONNECTOR_PROP_COUNT, // keep last
};

enum glider_drm_crtc_prop {
	GLIDER_DRM_CRTC_MODE_ID,
	GLIDER_DRM_CRTC_ACTIVE,
	GLIDER_DRM_CRTC_PROP_COUNT, // keep last
};

enum glider_drm_plane_prop {
	GLIDER_DRM_PLANE_TYPE,
	GLIDER_DRM_PLANE_IN_FORMATS,
	GLIDER_DRM_PLANE_PROP_COUNT, // keep last
};

struct glider_drm_prop_spec {
	const char *name;
	bool required;
};

extern const struct glider_drm_prop_spec
	glider_drm_connector_props[GLIDER_DRM_CONNECTOR_PROP_COUNT];
extern const struct glider_drm_prop_spec
	glider_drm_crtc_props[GLIDER_DRM_CRTC_PROP_COUNT];
extern const struct glider_drm_prop_spec
	glider_drm_plane_props[GLIDER_DRM_PLANE_PROP_COUNT];

struct glider_drm_prop {
	uint32_t id;
	uint64_t current, pending, initial;
};

enum glider_drm_buffer_state {
	GLIDER_DRM_BUFFER_UNLOCKED,
	GLIDER_DRM_BUFFER_PENDING, // next commit will submit this buffer
	GLIDER_DRM_BUFFER_QUEUED, // queued to KMS for display
	GLIDER_DRM_BUFFER_CURRENT, // current front buffer
};

struct glider_drm_buffer {
	struct glider_drm_device *device;
	struct wlr_buffer *buffer;
	struct wl_list link;

	struct gbm_bo *gbm;
	uint32_t id;

	struct wl_listener destroy;
};

/* Each time a buffer is attached to a CRTC, an attachment is created. We need
 * to track where the buffer is attached to properly release it when
 * page-flipping or attaching another buffer. */
struct glider_drm_attachment {
	enum glider_drm_buffer_state state;
	struct glider_drm_buffer *buffer;
	struct liftoff_layer *layer;
};

struct glider_drm_plane {
	struct glider_drm_device *device;
	uint32_t id;
	drmModePlane *plane;
	struct glider_drm_prop props[GLIDER_DRM_PLANE_PROP_COUNT];
	struct wlr_drm_format_set formats;
};

struct glider_drm_crtc {
	struct glider_drm_device *device;
	uint32_t id;
	drmModeCrtc *crtc;
	struct glider_drm_prop props[GLIDER_DRM_CRTC_PROP_COUNT];

	struct glider_drm_plane *primary_plane;

	struct glider_drm_attachment *attachments;
	size_t attachments_cap;

	struct liftoff_output *liftoff_output;
};

struct glider_drm_mode {
	struct wlr_output_mode wlr_mode;
	drmModeModeInfo drm_mode;
};

struct glider_drm_connector {
	struct wlr_output output; // only valid if connected

	struct glider_drm_device *device;
	uint32_t id;
	struct wl_list link;
	struct glider_drm_prop props[GLIDER_DRM_CONNECTOR_PROP_COUNT];

	drmModeConnection connection;
	uint32_t possible_crtcs;
	struct glider_drm_crtc *crtc; // NULL if disabled

	struct glider_drm_mode *modes;
	size_t modes_len;
};

struct glider_drm_device {
	struct glider_drm_backend *backend;
	int fd;
	struct wl_event_source *event_source;

	bool cap_addfb2_modifiers;

	struct gbm_device *gbm;
	struct wlr_drm_format_set formats; // union of all planes formats

	struct wl_list buffers;
	struct wl_list connectors;

	struct glider_drm_crtc *crtcs;
	size_t crtcs_len;

	struct glider_drm_plane *planes;
	size_t planes_len;

	struct liftoff_device *liftoff_device;

	struct wl_listener invalidated;
};

struct glider_drm_backend {
	struct wlr_backend base;

	struct wl_display *display;
	struct wlr_session *session;
	struct glider_drm_device devices[8];
	size_t devices_len;

	struct wl_listener display_destroy;
	struct wl_listener session_destroy;
	struct wl_listener session_signal;
};

struct wlr_backend *glider_drm_backend_create(struct wl_display *display,
	struct wlr_session *session);
int glider_drm_backend_get_render_fd(struct wlr_backend *backend);

const struct wlr_drm_format_set *glider_drm_connector_get_primary_formats(
	struct wlr_output *output);
struct liftoff_output *glider_drm_connector_get_liftoff_output(
	struct wlr_output *output);
bool glider_drm_connector_attach(struct wlr_output *output,
	struct wlr_buffer *buffer, struct liftoff_layer *layer);

bool init_drm_device(struct glider_drm_device *device,
	struct glider_drm_backend *backend, int fd);
void finish_drm_device(struct glider_drm_device *device);
bool refresh_drm_device(struct glider_drm_device *device);

struct glider_drm_connector *create_drm_connector(
	struct glider_drm_device *device, uint32_t id);
void destroy_drm_connector(struct glider_drm_connector *conn);
bool refresh_drm_connector(struct glider_drm_connector *conn);
void handle_drm_connector_page_flip(struct glider_drm_connector *conn,
	unsigned seq, struct timespec *t);

bool init_drm_crtc(struct glider_drm_crtc *crtc,
	struct glider_drm_device *device, uint32_t id);
void finish_drm_crtc(struct glider_drm_crtc *crtc);
void handle_drm_crtc_page_flip(struct glider_drm_crtc *crtc,
	unsigned seq, struct timespec *t);
bool set_drm_crtc_mode(struct glider_drm_crtc *crtc,
	const struct glider_drm_mode *mode);

bool init_drm_plane(struct glider_drm_plane *plane,
	struct glider_drm_device *device, uint32_t id);
void finish_drm_plane(struct glider_drm_plane *plane);

struct glider_drm_buffer *get_or_create_drm_buffer(
	struct glider_drm_device *device, struct wlr_buffer *buffer);
void unlock_drm_attachment(struct glider_drm_attachment *att);

bool init_drm_props(struct glider_drm_prop *props,
	const struct glider_drm_prop_spec *prop_specs, size_t props_len,
	struct glider_drm_device *device, uint32_t obj_id, uint32_t obj_type);
bool apply_drm_props(struct glider_drm_prop *props, size_t props_len,
	uint32_t obj_id, drmModeAtomicReq *req);
void move_drm_prop_values(struct glider_drm_prop *props, size_t props_len,
	bool commit);

#endif
