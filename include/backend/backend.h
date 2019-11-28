#ifndef GLIDER_BACKEND_BACKEND_H
#define GLIDER_BACKEND_BACKEND_H

#include <libliftoff.h>
#include <wlr/backend/interface.h>
#include <xf86drmMode.h>

struct glider_drm_backend;
struct glider_drm_device;

enum glider_drm_plane_prop {
	GLIDER_DRM_PLANE_TYPE,
	GLIDER_DRM_PLANE_PROP_COUNT, // keep last
};

extern const char *glider_drm_plane_prop_names[GLIDER_DRM_PLANE_PROP_COUNT];

struct glider_drm_prop {
	uint32_t id;
	uint64_t current, pending;
};

struct glider_drm_plane {
	struct glider_drm_device *device;
	uint32_t id;
	drmModePlane *plane;

	struct glider_drm_prop props[GLIDER_DRM_PLANE_PROP_COUNT];
};

struct glider_drm_crtc {
	struct glider_drm_device *device;
	uint32_t id;
	drmModeCrtc *crtc;

	struct liftoff_output *liftoff_output;
};

struct glider_drm_device {
	struct glider_drm_backend *backend;
	int fd;

	struct glider_drm_crtc *crtcs;
	size_t crtcs_len;

	struct glider_drm_plane *planes;
	size_t planes_len;

	struct liftoff_device *liftoff_device;

	struct wl_listener invalidated;
};

struct glider_drm_backend {
	struct wlr_backend base;

	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct glider_drm_device devices[8];
	size_t devices_len;

	struct wl_listener display_destroy;
	struct wl_listener session_destroy;
	struct wl_listener session_signal;
};

struct wlr_backend *glider_drm_backend_create(struct wl_display *display,
	struct wlr_session *session);

bool init_drm_device(struct glider_drm_device *device,
	struct glider_drm_backend *backend, int fd);
void finish_drm_device(struct glider_drm_device *device);

bool init_drm_crtc(struct glider_drm_crtc *crtc,
	struct glider_drm_device *device, uint32_t id);
void finish_drm_crtc(struct glider_drm_crtc *crtc);

bool init_drm_plane(struct glider_drm_plane *plane,
	struct glider_drm_device *device, uint32_t id);
void finish_drm_plane(struct glider_drm_plane *plane);

bool init_drm_props(struct glider_drm_prop *props, const char **prop_names,
	size_t props_len, struct glider_drm_device *device,
	uint32_t obj_id, uint32_t obj_type);

#endif
