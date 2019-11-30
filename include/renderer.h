#ifndef GLIDER_RENDERER_H
#define GLIDER_RENDERER_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <wlr/render/egl.h>

struct glider_renderer_buffer {
	struct glider_buffer *buffer;
	struct glider_renderer *renderer;
	struct wl_list link;

	EGLImageKHR egl_image;
	GLuint gl_texture;
	GLuint gl_fbo;

	// TODO: destroy listener
};

struct glider_renderer {
	struct wlr_renderer *renderer;
	struct wlr_egl egl;

	struct wl_list buffers;
	struct glider_renderer_buffer *current_buffer;
};

struct glider_renderer *glider_gbm_renderer_create(
	struct glider_allocator *alloc);
void glider_renderer_destroy(struct glider_renderer *renderer);
bool glider_renderer_begin(struct glider_renderer *renderer,
	struct glider_buffer *buffer);
void glider_renderer_end(struct glider_renderer *renderer);

#endif
