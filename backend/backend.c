#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
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
	wl_signal_emit(&wlr_backend->events.destroy, backend);
	wl_list_remove(&backend->display_destroy.link);
	wl_list_remove(&backend->session_destroy.link);
	wl_list_remove(&backend->session_signal.link);
	for (size_t i = 0; i < backend->devices_len; i++) {
		finish_drm_device(&backend->devices[i]);
	}
	free(backend);
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct glider_drm_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->base);
}

static void handle_session_destroy(struct wl_listener *listener, void *data) {
	struct glider_drm_backend *backend =
		wl_container_of(listener, backend, session_destroy);
	backend_destroy(&backend->base);
}

static void handle_session_signal(struct wl_listener *listener, void *data) {
	struct glider_drm_backend *backend =
		wl_container_of(listener, backend, session_signal);
	// TODO
}

struct wlr_backend *glider_drm_backend_create(struct wl_display *display,
		struct wlr_session *session) {
	struct glider_drm_backend *backend = calloc(1, sizeof(*backend));
	if (backend == NULL) {
		return NULL;
	}
	wlr_backend_init(&backend->base, &backend_impl);

	backend->session = session;

	int fds[sizeof(backend->devices) / sizeof(backend->devices[0])];
	size_t fds_len = wlr_session_find_gpus(session,
		sizeof(fds) / sizeof(fds[0]), fds);
	if (fds_len == 0) {
		wlr_log(WLR_ERROR, "Session returned zero GPUs");
		goto error_device;
	}

	for (size_t i = 0; i < fds_len; i++) {
		if (!init_drm_device(&backend->devices[i], backend, fds[i])) {
			goto error_device;
		}
		backend->devices_len++;
	}

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	backend->session_destroy.notify = handle_session_destroy;
	wl_signal_add(&session->events.destroy, &backend->session_destroy);

	backend->session_signal.notify = handle_session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	return &backend->base;

error_device:
	for (size_t i = backend->devices_len; i < fds_len; i++) {
		wlr_session_close_file(session, fds[i]);
	}
	for (size_t i = 0; i < backend->devices_len; i++) {
		finish_drm_device(&backend->devices[i]);
	}
	return NULL;
}

int glider_drm_backend_get_primary_fd(struct wlr_backend *wlr_backend) {
	struct glider_drm_backend *backend =
		get_drm_backend_from_backend(wlr_backend);
	return backend->devices[0].fd;
}
