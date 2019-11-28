#include <stdlib.h>
#include "allocator.h"

struct glider_allocator *glider_gbm_allocator_create(int fd) {
	struct glider_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}

	alloc->gbm_device = gbm_create_device(fd);
	if (alloc->gbm_device == NULL) {
		free(alloc);
		return NULL;
	}

	return alloc;
}

void glider_allocator_destroy(struct glider_allocator *alloc) {
	gbm_device_destroy(alloc->gbm_device);
	free(alloc);
}
