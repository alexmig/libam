#include <stdlib.h>
#include <string.h>

#include "libam/libam_cqueue.h"

/**
 * Allocates queue memory and readies queue for use
 * Not thread safe
 * Returns Pointer to new cqueue of size (capacity) / NULL on error
 *
 * WARNING: Queue must be allocated to accommodate all possible concurrent adds
 */
amcqueue_t* amcqueue_alloc(uint64_t capacity)
{
	//uint64_t i;
	uint64_t size;
	amcqueue_t* cq;

	capacity++; // We keep one empty at all times
	size = sizeof(amcqueue_t) + (sizeof(cq->data[0]) * capacity);
	cq = malloc(size);
	if (cq == NULL)
		return NULL;
	memset(cq, 0, size);
	/*for (i = 0; i < capacity; i++)
		cq->data[i] = NULL;
	cq->head = 0;
	cq->tail = 0; */
	cq->capacity = capacity;
	cq->read_lock = AMSPINLOCK_UNLOCKED;

	return cq;
}

/**
 * Releases resources of cqueue
 * Not thread safe
 * Returns RC_SUCCESS / RC_ERROR
 */
amrc_t amcqueue_free(amcqueue_t* cq)
{
	/*
	int rc;
	uint64_t size;

	size = sizeof(amcqueue_t) + (sizeof(cq->data[0]) * cq->capacity);
	rc = free(cq, size);
	if (rc != 0)
		return RC_ERROR;
	return RC_SUCCESS;
	*/
	free(cq);
	return AMRC_SUCCESS;
}

/**
 * Enqueues an element
 * Insertions are done to the tail
 *
 * WARNING: DOES NOT CHECK IF QUEUE IS FULL
 * 	This means that it'll (busy-)wait forever until slot becomes available
 *
 * Returns   RC_SUCCESS / RC_ERROR
 */
amrc_t amcqueue_enq(amcqueue_t* cq, void* data)
{
	uint64_t tail;
	uint64_t new_tail;

	// TODO: Remove check?
	if (data == NULL)
		return AMRC_ERROR;

	do {
		tail = cq->tail;
		new_tail = (tail + 1) % cq->capacity;
	} while (!amsync_swap(&cq->tail, tail, new_tail));

	while (!amsync_swap(&cq->data[tail], NULL, data));

	return AMRC_SUCCESS;
}

/**
 * Dequeues an element from cqueue
 * Removals are dome from the head
 * Returns   RC_SUCCESS / RC_EMPTY
 */
amrc_t amcqueue_deq(amcqueue_t* cq, void** data)
{
	uint64_t head;
	uint64_t new_head;
	void* ptr;
	void* volatile* pptr;

	amspinlock_lock(&cq->read_lock, 1);
	do {
		head = cq->head;
		new_head = (head + 1) % cq->capacity;
		if (head == cq->tail) {
			amspinlock_unlock(&cq->read_lock, 1);
			return AMRC_ERROR;
		}
	} while (!amsync_swap(&cq->head, head, new_head));
	amspinlock_unlock(&cq->read_lock, 1);

	// Now we need to wait for the data, if it's not there
	pptr = &cq->data[head];
try:
	ptr = *pptr;
	if (ptr == NULL)
		goto try;
	if (!amsync_swap(&cq->data[head], ptr, NULL))
		goto try;

	*data = ptr;
	return AMRC_SUCCESS;
};

