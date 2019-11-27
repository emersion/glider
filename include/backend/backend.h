#ifndef GLIDER_BACKEND_BACKEND_H
#define GLIDER_BACKEND_BACKEND_H

#include <wlr/backend/interface.h>

struct glider_drm_backend;

struct glider_drm_device {
	struct glider_drm_backend *backend;
	int fd;

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

#endif
