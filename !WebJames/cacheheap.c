
#include "oslib/osheap.h"

#include "cacheheap.h"


static void *cachestart;


int heap_largest() {
/* returns size of largest block that can be allocated from the heap */
	int max;

	if (xosheap_describe(cachestart, &max, NULL)) return NULL;
	return max;
}


void heap_release(void *buffer) {
/* releases a heap block */

	xosheap_free(cachestart, buffer);
}


void *heap_allocate(int size) {
/* allocates a heap block */
	void *buffer;

	if (xosheap_alloc(cachestart, size, &buffer))  return NULL;
	return buffer;
}


void heap_initialise(void *start, int size) {

	cachestart = start;
	xosheap_initialise(cachestart, size);
}
