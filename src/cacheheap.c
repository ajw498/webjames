/*
	$Id: cacheheap.c,v 1.1 2002/02/17 22:50:10 ajw Exp $
	Management of the cache heap
*/

#include <stdio.h>
#include <time.h>

#include "oslib/osheap.h"

#include "webjames.h"
#include "stat.h"
#include "cacheheap.h"


static void *cachestart;


int heap_largest()
/* returns size of largest block that can be allocated from the heap */
{
	int max;

	if (E(xosheap_describe(cachestart, &max, NULL))) return NULL;
	return max;
}


void heap_release(void *buffer)
/* releases a heap block */
{
	EV(xosheap_free(cachestart, buffer));
}


void *heap_allocate(int size)
/* allocates a heap block */
{
	void *buffer;

	if (xosheap_alloc(cachestart, size, &buffer))  return NULL;
	return buffer;
}


void heap_initialise(void *start, int size)
{
	cachestart = start;
	EV(xosheap_initialise(cachestart, size));
}
