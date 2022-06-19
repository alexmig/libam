#include <stdlib.h>
#include <string.h>

#include "libam/libam_atomic.h"
#include "libam/libam_stack.h"

/**
 * Allocates stack resources
 * Not thread safe
 * Returns Pointer to new stack / NULL on error
 *
 * WARNING: Size of queue cannot be changed once allocated
 */
amstack_t* amstack_alloc(uint64_t capacity)
{
	uint64_t size;
	amstack_t* stk;

	size = sizeof(amstack_t) + (sizeof(stk->data[0]) * capacity);
	stk = malloc(size);
	if (stk == NULL)
		return NULL;
	memset(stk, 0, size);
	stk->capacity = capacity;
	return stk;
}

/**
 * Releases resources of cqueue
 * Not thread safe
 */
amrc_t amstack_free(amstack_t* stk)
{
	free(stk);
	return AMRC_SUCCESS;
}

/**
 * Pushes a pointer to the top of the stack
 * Does not support NULL pointers
 * Returns   AMRC_SUCCESS / AMRC_ERROR when stack is full or pointer is NULL
 */
amrc_t amstack_push(amstack_t* stk, void* data)
{
	uint64_t size;
	uint64_t new_size;

	if (data == NULL)
		return AMRC_ERROR;

	/* Obtain the slot exclusively */
	do {
		size = stk->size;
		if (size >= stk->capacity)
			return AMRC_ERROR;
		new_size = size + 1;
	} while (!amsync_swap(&stk->size, size, new_size));

	/* Make sure the threads that had that spot exclusively before are done */
	while (!amsync_swap(&stk->data[size], NULL, data));

	return AMRC_SUCCESS;
}

/**
 * Pop a pointer from the stack
 * Returns   AMRC_SUCCESS / AMRC_ERROR when empty
 */
amrc_t amstack_pop(amstack_t* stk, void** data)
{
	uint64_t size;
	uint64_t new_size;
	void* ptr;
	void* volatile* pptr;

	if (data == NULL)
		return AMRC_ERROR;
	*data = NULL;

	/* Obtain the slot exclusively */
	do {
		size = stk->size;
		if (size == 0)
			return AMRC_ERROR;
		new_size = size - 1;
	} while (!amsync_swap(&stk->size, size, new_size));

	/* Make sure the threads that had that spot exclusively before are done */
	pptr = &stk->data[new_size];
	while (1) {
		ptr = *pptr;
		if (ptr == NULL)
			continue;
		if (amsync_swap(&stk->data[new_size], ptr, NULL))
			break;
	}

	*data = ptr;
	return AMRC_SUCCESS;
}

/**
 * Returns current stack size
 */
uint64_t amstack_get_size(amstack_t* stk)
{
	return stk->size;
}
