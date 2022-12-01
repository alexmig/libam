#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libam/libam_time.h"
#include "libam/libam_tree.h"

#define MMAX(a, b) ((a) < (b) ? (b) : (a))

typedef struct amt3t {
	amtree_node_t node;

	// Validation
	uint64_t depth;
	uint64_t count;

	// Insertion
	uint64_t is_added:1;
} amt3t_t;

typedef enum order {
	SEQ_UP,
	SEQ_DOWN,
	ALT,
	ALT_RAND,
	RAND,
} order_t;

static void amtree_node_print(uint64_t depth, const amtree_node_t* n)
{
	for (; depth > 1; depth--)
		printf("\t");

	if (depth == 1) {
		printf("+-------- ");
	}

	if (n == NULL)
		printf("NULL\n");
	else
		printf("%lx [%d][%p - p %p, l %p, r %p]\n", n->key, n->balance,
				n, n->parent, n->left, n->right);
}

static void amtree_print_depth(const amtree_node_t* n, uint64_t depth)
{
	amtree_node_print(depth, n);
	if (n == NULL)
		return;
	amtree_print_depth(n->left, depth + 1);
	amtree_print_depth(n->right, depth + 1);
}

void amtree_print(const amtree_t* tree, const char* msg)
{
	printf("Printing tree, count %u: '%s'\n", tree->count, msg);
	amtree_print_depth(tree->root, 0);
	printf("Done\n");
	fflush(stdout);
}

uint64_t amtree_depth(amtree_t* tree, amtree_node_t* node)
{
	amt3t_t* owner;
	uint64_t ld;
	uint64_t rd;

	if (node == NULL)
		return 0;
	owner = container_of(node, amt3t_t, node);

	ld = amtree_depth(tree, node->left);
	rd = amtree_depth(tree, node->right);
	owner->depth = 1 + MMAX(ld, rd);

	owner->count = 1;
	if (node->left) owner->count += container_of(node->left, amt3t_t, node)->count;
	if (node->right) owner->count += container_of(node->right, amt3t_t, node)->count;

	if (node->balance != (int)rd - (int)ld) {
		printf("Balance %d != expected %d on node %lx\n", node->balance, (int)rd - (int)ld, node->key);
		goto error;
	}
	if (node->balance >= 2 || node->balance <= -2) {
		printf("Invalid balance %d on node %lx\n", node->balance, node->key);
		goto error;
	}

	return owner->depth;

error:
	amtree_print(tree, "Error tree");
	fflush(stdout);
	abort();
	return -1;
}

void amtree_validate(amtree_t* tree, uint64_t ex_count)
{
	amt3t_t* owner;
	uint64_t depth;

	amtree_depth(tree, tree->root);
	if (ex_count != tree->count) {
		printf("Count %u != expected %lu\n", tree->count, ex_count);
		goto error;
	}

	if (tree->root != NULL && ex_count == 0) {
		printf("Count 0 while root exists\n");
		goto error;
	}

	if (tree->root == NULL && ex_count != 0) {
		printf("Count exists while root null\n");
		goto error;
	}

	if (tree->root != NULL) {
		owner = container_of(tree->root, amt3t_t, node);
		if (owner->count != ex_count) {
			printf("Actual count %lu != tree %lu\n", owner->count, ex_count);
			goto error;
		}

		depth = log2(ex_count) + 1;
		if (owner->depth > depth * 2) {
			printf("Depth %lu > expected %lu (count %lu)\n", owner->depth, depth * 2, ex_count);
			goto error;
		}
	}

	return;

error:
	amtree_print(tree, "Error tree");
	fflush(stdout);
	abort();
}

void amtree_validate_find(amtree_t* tree, uint64_t key, uint8_t ex_res)
{
	amtree_node_t* node;

	node = amtree_find_key(tree, key);
	if (node == NULL && ex_res) {
		printf("Expected value %lu not found\n", key);
		goto error;
	}
	if (node != NULL && !ex_res) {
		printf("Unexpected value %lu found\n", key);
		goto error;
	}

	return;

error:
	amtree_print(tree, "Error tree");
	fflush(stdout);
	abort();
}

static int test_add_seq(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order)
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t ii;
	uint64_t r_i;

	amtree_validate(tree, 0);
	amtree_validate_find(tree, 1, 0);
	for (i = 0; i < elements; i++) {
		ii = (order == SEQ_UP ? i : elements - 1 - i);
		amtree_validate_find(tree, nodes[ii].node.key, 0);
		rc = amtree_insert(tree, &nodes[ii].node);
		if (rc != NULL) {
			printf("Failed to insert unique entry %lx!\n", nodes[ii].node.key);
			return -1;
		}
		nodes[ii].is_added = 1;
		amtree_validate(tree, i + 1);
		amtree_validate_find(tree, nodes[ii].node.key, 1);

		if (i > 0) {
			r_i = rand() % i;
			ii = (order == SEQ_UP ? r_i : elements - 1 - r_i);
			amtree_validate_find(tree, nodes[ii].node.key, 1);
		}
	}

	return 0;
}

static int test_del_seq(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order)
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t ii;
	uint64_t expect = tree->count;
	uint64_t r_i;

	amtree_validate(tree, expect);
	for (i = 0; i < elements; i++) {
		ii = (order == SEQ_UP ? i : elements - 1 - i);

		amtree_validate_find(tree, nodes[ii].node.key, nodes[ii].is_added);
		rc = amtree_delete_key(tree, nodes[ii].node.key);
		if (!nodes[ii].is_added) {
			if (rc != NULL) {
				printf("Deleted already deleted node %lu [%p]\n", rc->key, rc);
				return -1;
			}
		}
		else {
			if (rc == NULL) {
				printf("Failed to delete node %lu [%p]\n", nodes[ii].node.key, &nodes[ii].node);
				return -1;
			}
			if (rc != &nodes[ii].node) {
				printf("Deleted node %lu [%p] != expected %lu [%p]\n", rc->key, rc, nodes[ii].node.key, &nodes[ii].node);
				return -1;
			}
			nodes[ii].is_added = 0;
			expect--;
		}
		amtree_validate(tree, expect);
		amtree_validate_find(tree, nodes[ii].node.key, 0);

		if (i > 0) {
			r_i = rand() % i;
			ii = (order == SEQ_UP ? r_i : elements - 1 - r_i);
			amtree_validate_find(tree, nodes[ii].node.key, 0);
		}
	}

	return 0;
}

static int test_add_alt(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order)
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t inserted = 0;
	uint64_t target = elements;
	uint64_t i_l;
	uint64_t i_r;
	uint8_t is_left;

	amtree_validate(tree, 0);
	amtree_validate_find(tree, 1, 0);

	if (order == ALT_RAND)
		target = (elements * 2) / 3;
	i_l = 0;
	i_r = elements - 1;
	is_left = 0;
	i = 0;
	while (inserted < target) {
		// Determine I
		if (order == ALT_RAND) {
			if (is_left) {
				if (i != 0)
					i = rand() % i;
			}
			else
				i += rand() % (elements - i);
		}
		else {
			if (is_left) {
				i = i_l;
				i_l += 1;
			}
			else {
				i = i_r;
				i_r -= 1;
			}
		}
		is_left = !is_left;

		amtree_validate_find(tree, nodes[i].node.key, nodes[i].is_added);
		rc = amtree_insert(tree, &nodes[i].node);
		if (nodes[i].is_added) {
			if (rc == NULL) {
				printf("Inserted already inserted node %lx\n", nodes[i].node.key);
				return -1;
			}
		}
		else {
			if (rc != NULL) {
				printf("Failed to insert new node %lx\n", nodes[i].node.key);
				return -1;
			}
			nodes[i].is_added = 1;
			inserted += 1;
		}
		amtree_validate(tree, inserted);
	}

	return 0;
}

static int test_del_alt(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order)
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t deleted = 0;
	uint64_t target = 0;
	uint64_t i_l;
	uint64_t i_r;
	uint8_t is_left;

	amtree_validate(tree, elements - deleted);

	target = (elements * 2) / 3;
	i_l = 0;
	i_r = elements - 1;
	is_left = 0;
	i = 0;
	while (deleted < target) {
		// Determine I
		if (order == ALT_RAND) {
			if (is_left) {
				if (i != 0)
					i = rand() % i;
			}
			else
				i += rand() % (elements - i);
		}
		else {
			if (is_left) {
				i = i_l;
				i_l += 1;
			}
			else {
				i = i_r;
				i_r -= 1;
			}
		}
		is_left = !is_left;

		amtree_validate_find(tree, nodes[i].node.key, nodes[i].is_added);
		rc = amtree_delete_key(tree, nodes[i].node.key);
		if (!nodes[i].is_added) {
			if (rc != NULL) {
				printf("Deleted already deleted node %lu [%p]\n", rc->key, rc);
				return -1;
			}
		}
		else {
			if (rc == NULL) {
				printf("Failed to delete node %lu [%p]\n", nodes[i].node.key, &nodes[i].node);
				return -1;
			}
			if (rc != &nodes[i].node) {
				printf("Deleted node %lu [%p] != expected %lu [%p]\n", rc->key, rc, nodes[i].node.key, &nodes[i].node);
				return -1;
			}
			nodes[i].is_added = 0;
			deleted += 1;
		}
		amtree_validate(tree, elements - deleted);
	}

	return 0;
}

static int test_add_rand(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order __attribute__((unused)))
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t inserted = 0;
	uint64_t target = (elements * 2) / 3;

	amtree_validate(tree, inserted);
	while (inserted < target) {
		i = rand() % elements;
		amtree_validate_find(tree, nodes[i].node.key, nodes[i].is_added);

		rc = amtree_insert(tree, &nodes[i].node);
		if (nodes[i].is_added) {
			if (rc == NULL) {
				printf("Inserted already inserted node %lx\n", nodes[i].node.key);
				return -1;
			}
		}
		else {
			if (rc != NULL) {
				printf("Failed to insert new node %lx\n", nodes[i].node.key);
				return -1;
			}
			nodes[i].is_added = 1;
			inserted += 1;
		}
		amtree_validate(tree, inserted);
	}

	return 0;
}

static int test_del_rand(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order __attribute__((unused)))
{
	amtree_node_t* rc;
	uint64_t i;
	uint64_t deleted = 0;
	uint64_t target = (elements * 2) / 3;

	amtree_validate(tree, elements - deleted);
	while (deleted < target) {
		i = rand() % elements;
		amtree_validate_find(tree, nodes[i].node.key, nodes[i].is_added);

		rc = amtree_delete_key(tree, nodes[i].node.key);
		if (nodes[i].is_added) {
			if (rc == NULL) {
				printf("Failed to find and delete node %lx\n", nodes[i].node.key);
				return -1;
			}
			if (rc != &nodes[i].node) {
				printf("Deleted node %lu [%p] != expected %lu [%p]\n", rc->key, rc, nodes[i].node.key, &nodes[i].node);
				return -1;
			}
			nodes[i].is_added = 0;
			deleted += 1;
		}
		else {
			if (rc != NULL) {
				printf("Deleted node %lu [%p] not in tree\n", rc->key, rc);
				return -1;
			}
		}
		amtree_validate(tree, elements - deleted);
	}

	return 0;
}

typedef int (*test_cb_t)(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order);

static int test_add_start(amtree_t* tree, amt3t_t* nodes, uint64_t elements, order_t order, test_cb_t cb)
{
	uint64_t i;
	amtree_init(tree);
	memset(nodes, 0, sizeof(*nodes) * elements);
	for (i = 0; i <= elements; i++)
		nodes[i].node.key = i;
	return cb(tree, nodes, elements, order);
}

static int test_del_start(amtree_t* otree, amtree_t* tree,
	amt3t_t* onodes, amt3t_t* nodes,
	uint64_t elements, order_t order, test_cb_t cb)
{
	memcpy(tree, otree, sizeof(*tree));
	memcpy(nodes, onodes, sizeof(*nodes) * elements);
	return cb(tree, nodes, elements, order);
}

static int test_add(uint64_t elements)
{
	int ret = -1;
	amt3t_t* nodes = NULL;
	amtree_t tree;

	amtree_init(&tree);

	if (elements == 0) {
		printf("Can't carry out a test with no elements\n");
		return 0;
	}

	nodes = malloc(sizeof(*nodes) * elements);
	if (nodes == NULL) {
		printf("Could not allocate memory\n");
		goto error;
	}
	srand(amtime_now());

	if (test_add_start(&tree, nodes, elements, SEQ_UP, test_add_seq)) goto error;
	if (test_add_start(&tree, nodes, elements, SEQ_DOWN, test_add_seq)) goto error;
	if (test_add_start(&tree, nodes, elements, ALT, test_add_alt)) goto error;
	if (test_add_start(&tree, nodes, elements, ALT_RAND, test_add_alt)) goto error;
	if (test_add_start(&tree, nodes, elements, RAND, test_add_rand)) goto error;

	ret = 0;
error:
	if (ret != 0)
		amtree_print(&tree, "Offending tree\n");
	if (nodes != NULL)
		free(nodes);
	fflush(stdout);
	return ret;
}

static int test_del(uint64_t elements)
{
	int ret = -1;
	amt3t_t* nodes = NULL;
	amtree_t tree;
	amt3t_t* nodes_copy = NULL;
	amtree_t tree_copy;
	order_t iorder;
	int rc = 0;

	amtree_init(&tree);

	if (elements == 0) {
		printf("Can't carry out a test with no elements\n");
		return 0;
	}

	nodes = malloc(sizeof(*nodes) * elements);
	if (nodes == NULL) {
		printf("Could not allocate memory\n");
		goto error;
	}

	nodes_copy = malloc(sizeof(*nodes_copy) * elements);
	if (nodes_copy == NULL) {
		printf("Could not allocate memory\n");
		goto error;
	}

	srand(amtime_now());
	iorder = rand() % ALT_RAND;
	switch (iorder) {
	case SEQ_UP:	rc = test_add_start(&tree, nodes, elements, SEQ_UP, test_add_seq); break;
	case SEQ_DOWN:	rc = test_add_start(&tree, nodes, elements, SEQ_DOWN, test_add_seq); break;
	case ALT:		rc = test_add_start(&tree, nodes, elements, ALT, test_add_alt); break;
	case ALT_RAND:
	case RAND:		rc = -1; break;
	}
	if (rc != 0) {
		printf("Failed to populate tree for del test via order %d\n", iorder);
		goto error;
	}
	memcpy(&tree_copy, &tree, sizeof(tree));
	memcpy(nodes_copy, nodes, sizeof(*nodes) * elements);

	if (test_del_start(&tree_copy, &tree, nodes_copy, nodes, elements, SEQ_UP, test_del_seq)) goto error;
	if (test_del_start(&tree_copy, &tree, nodes_copy, nodes, elements, SEQ_DOWN, test_del_seq)) goto error;
	if (test_del_start(&tree_copy, &tree, nodes_copy, nodes, elements, ALT, test_del_alt)) goto error;
	if (test_del_start(&tree_copy, &tree, nodes_copy, nodes, elements, ALT_RAND, test_del_alt)) goto error;
	if (test_del_start(&tree_copy, &tree, nodes_copy, nodes, elements, RAND, test_del_rand)) goto error;

	ret = 0;
error:
	if (ret != 0)
		amtree_print(&tree, "Offending tree\n");
	if (nodes != NULL)
		free(nodes);
	if (nodes_copy != NULL)
		free(nodes_copy);
	fflush(stdout);
	return ret;
}

int main()
{
	amtime_t start = amtime_now();
	int i;

	printf("libam testing of amtree_t starting...\n");
	for (i = 1; i <= 17; i++) {
		if (test_add(i))
			return -1;
		if (test_del(i))
			return -1;
	}

	for (i = 20; i < 1000; i += 17) {
		if (test_add(i))
			return -1;
		if (test_del(i))
			return -1;
	}

	printf("libam testing of amtree_t done successfully (%.2lf seconds)!\n", ((double)(amtime_now() - start)) / ((double)AMTIME_SEC));
	return 0;
}

