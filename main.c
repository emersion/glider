#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "gbm_allocator.h"
#include "backend/backend.h"
#include "renderer.h"
#include "server.h"

int main(int argc, char *argv[]) {
	struct glider_server server = {0};
	wl_list_init(&server.outputs);

	wlr_log_init(WLR_DEBUG, NULL);

	const char *startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}

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
	struct glider_gbm_allocator *gbm_allocator =
		glider_gbm_allocator_create(fd);
	if (gbm_allocator == NULL) {
		return 1;
	}
	server.allocator = &gbm_allocator->base;

	server.renderer =
		glider_gbm_renderer_create(gbm_allocator->gbm_device);
	if (server.renderer == NULL) {
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer->renderer, server.display);

	wlr_compositor_create(server.display, server.renderer->renderer);

	server.xdg_shell = wlr_xdg_shell_create(server.display);

	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.new_xdg_surface.notify = handle_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface,
		&server.new_xdg_surface);

	if (!wlr_backend_start(server.backend)) {
		return 1;
	}

	const char *socket = wl_display_add_socket_auto(server.display);
	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
		socket);

	if (startup_cmd) {
		wlr_log(WLR_DEBUG, "Running startup command: %s", startup_cmd);
		pid_t pid = fork();
		if (pid < 0) {
			wlr_log_errno(WLR_ERROR, "fork failed");
		} else if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			wlr_log_errno(WLR_ERROR, "execl failed");
		}
	}

	wl_display_run(server.display);

	glider_renderer_destroy(server.renderer);
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	glider_allocator_destroy(server.allocator);
	return 0;
}
