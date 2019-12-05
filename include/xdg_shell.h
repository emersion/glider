#ifndef GLIDER_XDG_SHELL_H
#define GLIDER_XDG_SHELL_H

#include <wlr/types/wlr_xdg_shell.h>

struct glider_surface {
	struct wlr_surface *wlr_surface;

	struct wl_listener destroy;
};

#endif
