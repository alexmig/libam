#ifndef _LIBAM_STACK_H_
#define _LIBAM_STACK_H_

#include "libam_types.h"

/** Lock-free* stack implementation for n-producers and n-consumers
 * Useful for thread-safe, efficient implementations of pooled objects
 *
 * Allocation and destruction are not thread-safe
 *
 * The underlying implementation supplements locking with spinning, relying in sub-nanosecond response times of CPU cache.
 */

typedef struct amstack {
	uint64_t capacity;	// Not changing
	volatile uint64_t size; // Atomicity guarantee
	void* volatile data[0];
} amstack_t;

/**
 * Allocates stack resources
 * Not thread safe
 * Returns Pointer to new stack / NULL on error
 *
 * WARNING: Size of queue cannot be changed once allocated
 */
amstack_t* amstack_alloc(uint64_t capacity);

/**
 * Releases resources of cqueue
 * Not thread safe
 */
amrc_t amstack_free(amstack_t* stk);

/**
 * Pushes a pointer to the top of the stack
 * Does not support NULL pointers
 * Returns   AMRC_SUCCESS / AMRC_ERROR when stack is full or pointer is NULL
 */
amrc_t amstack_push(amstack_t* stk, void* data);

/**
 * Pop a pointer from the stack
 * Returns   AMRC_SUCCESS / AMRC_ERROR when empty
 */
amrc_t amstack_pop(amstack_t* stk, void** data);

#endif
