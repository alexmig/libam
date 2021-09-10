#include <stdlib.h>
#include <assert.h>

#include "libam/libam_lstack.h"
#include "libam/libam_atomic.h"

/**
 * Initializes and terminates a stack.
 *
 * WARNING: This operation is not threading-safe, all users of thread must be done between invokations
 */
void amlstack_init(amlstack_t* stk)
{
	stk->size = 0;
	stk->head = NULL;
}

/**
 * Insert a node into the stack
 * WARNING: This code does not check for double insertion.
 */
void amlstack_push(amlstack_t* stk, amlstack_node_t* node)
{
	amlstack_node_t* volatile * headptr = &stk->head;
	amlstack_node_t* head;

	assert(stk != NULL && node != NULL);

	do {
		head = *headptr;
		node->next = head;
	} while (!amsync_swap(headptr, head, node));
	amsync_inc(&stk->size);
}

/**
 * Pop a node from the stack
 * Returns NULL if empty.
 *
 * WARNING: When there are multiple consumers, this utilizes a spinlock!
 */
amlstack_node_t* amlstack_pop(amlstack_t* stk)
{
	amlstack_node_t* volatile * headptr = &stk->head;
	amlstack_node_t* head;
	amlstack_node_t* next;

	assert(stk != NULL);

	amspinlock_lock(&stk->consumer_lock, 1);
	do {

		head = *headptr;
		if (head == NULL)
			goto out;
		next = head->next;

		/* An erroneous tale of two threads
		 * --------------------------------
		 * Stack: node1 -> NULL
		 * Thread 1: stack.pop(), reaches this line, sleeps (head == node1, next == NULL)
		 * Thread 2: stack.pop(), succeeds
		 * Stack: NULL
		 * Thread 2: stack.push(node2)
		 * Stack: node2 - > NULL
		 * Thread 2: stack.push(node1) back in
		 * Stack: node1 -> node2 -> NULL
		 * Thread 1: Wakes up, (head == node1, next == NULL), sync swap succeeds when it really shouldn't
		 * Stack: NULL
		 * This is an error causing us to lose node2
		 *
		 * Until we figure something out, multiple consumers must lock
		 */

	} while (!amsync_swap(headptr, head, next));

	head->next = NULL;
	amsync_dec(&stk->size);

out:
	if (!amspinlock_unlock(&stk->consumer_lock, 1))
		abort();
	return head;
}
