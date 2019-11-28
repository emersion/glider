#ifndef GLIDER_ALLOCATOR_H
#define GLIDER_ALLOCATOR_H

#include <gbm.h>

// TODO: turn this into an interface
struct glider_allocator {
	struct gbm_device *gbm_device;
};

struct glider_allocator *glider_gbm_allocator_create(int fd);
void glider_allocator_destroy(struct glider_allocator *alloc);

#endif
