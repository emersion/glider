#ifndef GLIDER_RENDERER_H
#define GLIDER_RENDERER_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <stdbool.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_buffer.h>

struct glider_gl_renderer_buffer {
	struct wlr_buffer *buffer;
	struct glider_gl_renderer *renderer;
	struct wl_list link;

	EGLImageKHR egl_image;
	GLuint gl_texture;
	GLuint gl_fbo;

	struct wl_listener destroy;
};

struct glider_gl_renderer {
	struct wlr_renderer *renderer;
	struct wlr_egl egl;

	struct wl_list buffers;
	struct glider_gl_renderer_buffer *current_buffer;
};

struct glider_gl_renderer *glider_gl_gbm_renderer_create(struct gbm_device *device);
void glider_gl_renderer_destroy(struct glider_gl_renderer *renderer);
bool glider_gl_renderer_begin(struct glider_gl_renderer *renderer,
	struct wlr_buffer *buffer);
void glider_gl_renderer_end(struct glider_gl_renderer *renderer);

#endif
