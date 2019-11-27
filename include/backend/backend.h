#ifndef GLIDER_BACKEND_BACKEND_H
#define GLIDER_BACKEND_BACKEND_H

#include <wlr/backend/interface.h>

struct glider_drm_backend {
	struct wlr_backend base;

	struct wlr_renderer *renderer;

	struct wl_listener display_destroy;
};

struct wlr_backend *glider_drm_backend_create(struct wl_display *display);

#endif
