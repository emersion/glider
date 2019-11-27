#include <assert.h>
#include <stdlib.h>
#include "backend/backend.h"

static const struct wlr_backend_impl backend_impl;

struct glider_drm_backend *get_drm_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend->impl == &backend_impl);
	return (struct glider_drm_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct glider_drm_backend *backend =
		get_drm_backend_from_backend(wlr_backend);
	wl_list_remove(&backend->display_destroy.link);
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct glider_drm_backend *backend =
		get_drm_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct glider_drm_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->base);
}

struct wlr_backend *glider_drm_backend_create(struct wl_display *display) {
	struct glider_drm_backend *backend = calloc(1, sizeof(*backend));
	if (backend == NULL) {
		return NULL;
	}
	wlr_backend_init(&backend->base, &backend_impl);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->base;
}
