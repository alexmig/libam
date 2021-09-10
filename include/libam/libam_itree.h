#ifndef _LIBAM_ITREE_H_
#define _LIBAM_ITREE_H_

/* Exact same thing as amtree_t, only with a small added footprint per node to optimize iterations */

#include "libam/libam_types.h"
#include "libam/libam_list.h"

typedef struct amitree_node {
	struct amitree_node* parent;
	struct amitree_node* left; // Smaller
	struct amitree_node* right; // Larger
	amlink_t link; // next == larger
	uint64_t key;
	int balance; // +n = n more levels on the right, -n on the left
} amitree_node_t;

typedef struct amitree {
	amitree_node_t* root;
	amlist_t ordered_list; // next/first == smallest key
	uint32_t count;
} amitree_t;

#define amitree_root(tree)		(tree)->root
#define amitree_count(tree)		(tree)->count
#define amitree_is_empty(tree)	(amitree_count((tree)) == 0)

amrc_t amitree_init(amitree_t* tree);

amitree_node_t* amitree_find_key(const amitree_t* tree, const uint64_t key); // NULL if not found
amitree_node_t* amitree_find(const amitree_t* tree, const amitree_node_t* target); // NULL if not found

amitree_node_t* amitree_insert(amitree_t* tree, amitree_node_t* node); // Returns NULL on success / Existing node on duplicate
void amitree_delete(amitree_t* tree, amitree_node_t* node);
amitree_node_t* amitree_delete_key(amitree_t* tree, const uint64_t key);  // Returns deleted node / NULL if not in tree
void amitree_swap(amitree_t* tree, amitree_node_t* to_remove, amitree_node_t* to_insert); // Returns NULL on success / Existing node on duplicate

#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - (char *) &((type *)0)->member))
#endif

// NULL if empty
#define amitree_smallest(t) \
	((t) == NULL || amitree_is_empty((t)) ? NULL : container_of((t)->ordered_list.next, amitree_node_t, link))
#define amitree_first(t) amitree_smallest((t))

// NULL if empty
#define amitree_largest(t) \
		((t) == NULL || amitree_is_empty((t)) ? NULL : container_of((t)->ordered_list.prev, amitree_node_t, link))
#define amitree_last(t) amitree_largest((t))

// NULL on error
#define amitree_larger(t, n) \
	((t) == NULL || (n) == NULL || (n)->link.next == &(t)->ordered_list ? NULL : container_of((n)->link.next, amitree_node_t, link))
#define amitree_next(t, n) amitree_larger((t), (n))

// NULL on error
#define amitree_smaller(t, n) \
		((t) == NULL || (n) == NULL || (n)->link.prev == &(t)->ordered_list ? NULL : container_of((n)->link.prev, amitree_node_t, link))
#define amitree_prev(t, n) amitree_smaller((t), (n))

#endif
