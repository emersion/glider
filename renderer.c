#include <assert.h>
#include <gbm.h>
#include <stdlib.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "allocator.h"
#include "renderer.h"

struct glider_renderer *glider_gbm_renderer_create(struct gbm_device *device) {
	struct glider_renderer *renderer = calloc(1, sizeof(*renderer));
	if (renderer == NULL) {
		return NULL;
	}

	wl_list_init(&renderer->buffers);

	static EGLint config_attribs[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE,
	};

	// We're not going to allocate buffers with OpenGL, so the format doesn't
	// matter.
	renderer->renderer = wlr_renderer_autocreate(&renderer->egl,
		EGL_PLATFORM_GBM_MESA, device,
		config_attribs, GBM_FORMAT_ARGB8888);
	if (renderer->renderer == NULL) {
		free(renderer);
		return NULL;
	}

	return renderer;
}

static void renderer_buffer_destroy(struct glider_renderer_buffer *buf) {
	wl_list_remove(&buf->link);
	wl_list_remove(&buf->destroy.link);
	glDeleteFramebuffers(1, &buf->gl_fbo);
	glDeleteTextures(1, &buf->gl_texture);
	wlr_egl_destroy_image(&buf->renderer->egl, &buf->egl_image);
	free(buf);
}

void glider_renderer_destroy(struct glider_renderer *renderer) {
	wlr_egl_make_current(&renderer->egl, EGL_NO_SURFACE, NULL);
	struct glider_renderer_buffer *buf, *buf_tmp;
	wl_list_for_each_safe(buf, buf_tmp, &renderer->buffers, link) {
		renderer_buffer_destroy(buf);
	}
	wlr_renderer_destroy(renderer->renderer);
	wlr_egl_finish(&renderer->egl);
	free(renderer);
}

static struct glider_renderer_buffer *get_buffer(struct glider_renderer *renderer,
		struct glider_buffer *buffer) {
	struct glider_renderer_buffer *renderer_buffer;
	wl_list_for_each(renderer_buffer, &renderer->buffers, link) {
		if (renderer_buffer->buffer == buffer) {
			return renderer_buffer;
		}
	}
	return NULL;
}

static void handle_buffer_destroy(struct wl_listener *listener, void *data) {
	struct glider_renderer_buffer *buf =
		wl_container_of(listener, buf, destroy);
	renderer_buffer_destroy(buf);
}

static struct glider_renderer_buffer *renderer_buffer_create(
		struct glider_renderer *renderer, struct glider_buffer *buffer) {
	struct glider_renderer_buffer *renderer_buffer =
		calloc(1, sizeof(*renderer_buffer));
	if (renderer_buffer == NULL) {
		return NULL;
	}

	renderer_buffer->buffer = buffer;
	renderer_buffer->renderer = renderer;

	wlr_log(WLR_DEBUG, "Creating GL FBO for GBM buffer");

	struct wlr_dmabuf_attributes dmabuf;
	if (!glider_buffer_get_dmabuf(buffer, &dmabuf)) {
		goto error;
	}

	renderer_buffer->egl_image = wlr_egl_create_image_from_dmabuf(
		&renderer->egl, &dmabuf);
	if (renderer_buffer->egl_image == EGL_NO_IMAGE_KHR) {
		goto error;
	}

	if (!wlr_egl_make_current(&renderer->egl, EGL_NO_SURFACE, NULL)) {
		goto error;
	}

	glGenTextures(1, &renderer_buffer->gl_texture);
	glBindTexture(GL_TEXTURE_2D, renderer_buffer->gl_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// TODO: check for extension
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, renderer_buffer->egl_image);

	glGenFramebuffers(1, &renderer_buffer->gl_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, renderer_buffer->gl_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, renderer_buffer->gl_texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		wlr_log(WLR_ERROR, "Failed to create FBO");
		goto error;
	}

	renderer_buffer->destroy.notify = handle_buffer_destroy;
	wl_signal_add(&buffer->events.destroy, &renderer_buffer->destroy);

	wl_list_insert(&renderer->buffers, &renderer_buffer->link);

	return renderer_buffer;

error:
	free(renderer_buffer);
	return NULL;
}

bool glider_renderer_begin(struct glider_renderer *renderer,
		struct glider_buffer *buffer) {
	assert(renderer->current_buffer == NULL);

	struct glider_renderer_buffer *renderer_buffer =
		get_buffer(renderer, buffer);
	if (renderer_buffer == NULL) {
		renderer_buffer = renderer_buffer_create(renderer, buffer);
	}
	if (renderer_buffer == NULL) {
		return false;
	}

	if (!wlr_egl_make_current(&renderer->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, renderer_buffer->gl_fbo);
	wlr_renderer_begin(renderer->renderer, renderer_buffer->buffer->width,
		renderer_buffer->buffer->height);
	renderer->current_buffer = renderer_buffer;

	return true;
}

void glider_renderer_end(struct glider_renderer *renderer) {
	assert(renderer->current_buffer != NULL);
	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	wlr_renderer_end(renderer->renderer);
	renderer->current_buffer = NULL;
}
