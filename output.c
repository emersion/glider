#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include "server.h"

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct glider_output *output = wl_container_of(listener, output, destroy);
	wl_list_remove(&output->destroy.link);
	free(output);
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;

	struct glider_output *output = calloc(1, sizeof(*output));
	output->output = wlr_output;

	output->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
}
