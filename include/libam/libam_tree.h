#ifndef _LIBAM_TREE_H_
#define _LIBAM_TREE_H_

#include "libam/libam_types.h"

typedef struct amtree_node {
	struct amtree_node* parent;
	struct amtree_node* left; // Smaller
	struct amtree_node* right; // Larger
	uint64_t key;
	int balance; // +n = n more levels on the right, -n on the left
} amtree_node_t;

typedef struct amtree {
	amtree_node_t* root;
	uint32_t count;
} amtree_t;

#define amtree_init(tree) 	memset((tree), 0, sizeof(*tree))
#define amtree_root(tree)		(tree)->root
#define amtree_count(tree)	(tree)->count
#define amtree_is_empty(tree)	(amtree_count((tree)) == 0)

amtree_node_t* amtree_find_key(const amtree_t* tree, const uint64_t key); // NULL if not found
amtree_node_t* amtree_find(const amtree_t* tree, const amtree_node_t* target); // NULL if not found

amtree_node_t* amtree_insert(amtree_t* tree, amtree_node_t* node); // Returns NULL on success / Existing node on duplicate
void amtree_delete(amtree_t* tree, amtree_node_t* node);
amtree_node_t* amtree_delete_key(amtree_t* tree, const uint64_t key);  // Returns deleted node / NULL if not in tree
void amtree_swap(amtree_t* tree, amtree_node_t* to_remove, amtree_node_t* to_insert); // Returns NULL on success / Existing node on duplicate

#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - (char *) &((type *)0)->member))
#endif

#endif
