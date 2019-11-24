#include <wlr/util/log.h>
#include "server.h"

int main(int argc, char *argv[]) {
	struct glider_server server = {0};

	wlr_log_init(WLR_DEBUG, NULL);

	server.display = wl_display_create();
	if (server.display == NULL) {
		wlr_log(WLR_ERROR, "wl_display_create failed");
		return 1;
	}

	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	return 0;
}
