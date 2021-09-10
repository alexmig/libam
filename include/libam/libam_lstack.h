#ifndef _LIBAM_LSTACK_H_
#define _LIBAM_LSTACK_H_

/* Lock-free(ish) distributed stack
 * Multiple producers, single conusmer is totally lock free. (N:1)
 * Multiple producers, multiple conusmers is making use of a spinlock. (N:N)
 *
 * No memory allocation is happening within the data structure, it merely links existing structures
 *
 * Usage examples:

typedef struct object {
	amlstack_node_t node_member;
	int whatever;
} object_t;

{
	amlstack_t stack;
	object_t* obj;
	amlstack_node_t* node;

	amlstack_init(&stack);

	// Producing - Can be done from any number of threads
	obj = malloc(sizeof(*obj));
	obj.whatever = 1;
	amlstack_push(&stack, &obj.node_member);

	// Consuming - Must be done from a single thread
	node = amlstack_pop(&stack);
	if (node != NULL) {
		obj = container_of(node, object_t, node_member)
	}

	amlstack_term(&stack);
}
 */

#include "libam_types.h"
#include "libam_spinlock.h"

typedef struct amlstack_node {
	struct amlstack_node* volatile next;
} amlstack_node_t;

typedef struct amlstack {
	volatile uint64_t size;
	amspinlock_t consumer_lock;
	amlstack_node_t* volatile head;
} amlstack_t;

/**
 * Initializes and terminates a stack.
 *
 * WARNING: This operation is not threading-safe, all users of thread must be done between invokations
 */
void amlstack_init(amlstack_t* stk);
#define amlstack_term(stk) amlstack_init((stk))

/**
 * Insert a node into the stack
 * WARNING: This code does not check for double insertion.
 */
void amlstack_push(amlstack_t* stk, amlstack_node_t* node);

/**
 * Pop a node from the stack
 * Returns NULL if empty.
 *
 * WARNING: When there are multiple consumers, this utilizes a spinlock!
 */
amlstack_node_t* amlstack_pop(amlstack_t* stk);

/**
 * Usability macro
 */
#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - (char *) &((type *)0)->member))

#endif


#endif /* _LIBAM_LSTACK_H_ */
