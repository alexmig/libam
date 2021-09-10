#ifndef _LIBAM_CQUEUE_H_
#define _LIBAM_CQUEUE_H_

#include "libam_types.h"
#include "libam_spinlock.h"

typedef struct amcqueue {
	amspinlock_t read_lock;
	uint64_t capacity;	// Not changing
	volatile uint64_t head; // Atomicity guarantee
	volatile uint64_t tail; // Atomicity guarantee
	void* volatile data[0];
} amcqueue_t;

/**
 * Allocates queue memory and readies queue for use
 * Not thread safe
 * Returns Pointer to new cqueue of size (capacity) / NULL on error
 *
 * WARNING: Queue must be allocated to accommodate all possible concurrent adds
 */
amcqueue_t* amcqueue_alloc(uint64_t capacity);

/**
 * Releases resources of cqueue
 *
 * WARNING: Not thread safe
 */
amrc_t amcqueue_free(amcqueue_t* cq);

/**
 * Enqueues an element
 * Insertions are done to the tail
 *
 * WARNING: DOES NOT CHECK IF QUEUE IS FULL
 * 	This means that it'll (busy-)wait forever until slot becomes available
 *
 * Returns   AMRC_SUCCESS / AMRC_ERROR
 */
amrc_t amcqueue_enq(amcqueue_t* cq, void* data);

/**
 * Dequeues an element from cqueue
 * Removals are dome from the head
 * Returns   AMRC_SUCCESS / AMRC_ERROR when empty
 */
amrc_t amcqueue_deq(amcqueue_t* cq, void** data);

#endif
