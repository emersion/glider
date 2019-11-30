#include <wlr/util/log.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/libinput.h>
#include "allocator.h"
#include "backend/backend.h"
#include "renderer.h"
#include "server.h"

int main(int argc, char *argv[]) {
	struct glider_server server = {0};

	wlr_log_init(WLR_DEBUG, NULL);

	server.display = wl_display_create();
	if (server.display == NULL) {
		wlr_log(WLR_ERROR, "wl_display_create failed");
		return 1;
	}

	struct wlr_session *session = wlr_session_create(server.display);
	if (session == NULL) {
		return 1;
	}

	server.backend = wlr_multi_backend_create(server.display);

	struct wlr_backend *drm_backend =
		glider_drm_backend_create(server.display, session);
	if (drm_backend == NULL) {
		return 1;
	}
	wlr_multi_backend_add(server.backend, drm_backend);

	struct wlr_backend *libinput_backend =
		wlr_libinput_backend_create(server.display, session);
	if (libinput_backend == NULL) {
		return 1;
	}
	wlr_multi_backend_add(server.backend, libinput_backend);

	// TODO: multi-GPU
	int fd = glider_drm_backend_get_primary_fd(drm_backend);
	server.allocator = glider_gbm_allocator_create(fd);
	if (server.allocator == NULL) {
		return 1;
	}

	server.renderer = glider_gbm_renderer_create(server.allocator);
	if (server.renderer == NULL) {
		return 1;
	}

	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	if (!wlr_backend_start(server.backend)) {
		return 1;
	}

	wl_display_run(server.display);

	glider_renderer_destroy(server.renderer);
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	glider_allocator_destroy(server.allocator);
	return 0;
}
