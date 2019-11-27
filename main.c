#include <wlr/util/log.h>
#include <wlr/backend/multi.h>
#include "backend/backend.h"
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

	if (!wlr_backend_start(server.backend)) {
		return 1;
	}

	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	return 0;
}
