#include <stdio.h>

#include "swis.h"
#include "cacheheap.h"


static void *cachestart;


int heap_largest() {
// returns size of largest block that can be allocated from the heap
  int max;

  if (_swix(OS_Heap, _IN(0)|_IN(1)|_OUT(2), 1, cachestart, &max))
     return NULL;
  return max;
}


void heap_release(void *buffer) {
// releases a heap block

  _swix(OS_Heap, _IN(0)|_IN(1)|_IN(2), 3, cachestart, buffer);
}


void *heap_allocate(int size) {
// allocates a heap block
  void *buffer;

  if (_swix(OS_Heap, _IN(0)|_IN(1)|_IN(3)|_OUT(2),
                     2, cachestart, size, &buffer))  return NULL;
  return buffer;
}


void heap_initialise(void *start, int size) {

  cachestart = start;
  _swix(OS_Heap, _IN(0)|_IN(1)|_IN(3), 0, cachestart, size);
}
