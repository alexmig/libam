#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libam/libam_time.h"
#include "libam/libam_tree.h"
#include "libam/libam_itree.h"

/* This is counting on the fact that amitree_t is a complete copy of the fully-validated amtree_t */

#define MAX_ELEMENTS (1024)
#define REPS_MAX (8388608)

// This is needed to actually have the test work when building for production
#define ASSERT(x) { if (!(x)) abort(); }

typedef struct test_node {
	amtree_node_t reference_node;
	amitree_node_t test_node;
	ambool_t is_in;
	uint64_t key;
} test_node_t;

typedef struct test_tree {
	amtree_t reference_tree;
	amitree_t test_tree;
} test_tree_t;

int test_tree_validate_nodes(amtree_node_t* reference_node, amitree_node_t* test_node)
{
	uint64_t errors = 0;

	if (reference_node->key != test_node->key) errors++;
	if (reference_node->balance != test_node->balance) errors++;
	if ((reference_node->parent == NULL && test_node->parent != NULL) || (reference_node->parent != NULL && test_node->parent == NULL)) errors++;
	if (reference_node->parent != NULL)
		if (container_of(reference_node->parent, test_node_t, reference_node) != container_of(test_node->parent, test_node_t, test_node)) errors++;
	if ((reference_node->left == NULL && test_node->left != NULL) || (reference_node->left != NULL && test_node->left == NULL)) errors++;
	if (reference_node->left != NULL)
		if (container_of(reference_node->left, test_node_t, reference_node) != container_of(test_node->left, test_node_t, test_node)) errors++;
	if ((reference_node->right == NULL && test_node->right != NULL) || (reference_node->right != NULL && test_node->right == NULL)) errors++;
	if (reference_node->right != NULL)
		if (container_of(reference_node->right, test_node_t, reference_node) != container_of(test_node->right, test_node_t, test_node)) errors++;

	if (errors == 0 && reference_node->left != NULL)
		errors += test_tree_validate_nodes(reference_node->left, test_node->left);
	if (errors == 0 && reference_node->right != NULL)
		errors += test_tree_validate_nodes(reference_node->right, test_node->right);

	if (errors > 0) {
		printf("amitree validation failed\n");
		fflush(stdout);
		abort();
	}

	return errors;
}

int test_tree_validate_order(test_tree_t* tt)
{
	uint64_t last_key = 0;
	uint64_t target = amtree_count(&tt->reference_tree);
	amtree_node_t* reference_node;
	amitree_node_t* test_node;
	amlink_t* last_test_link = &tt->test_tree.ordered_list;
	test_node_t* common_node;
	uint64_t errors = 0;

	test_node = amitree_smallest(&tt->test_tree);
	while (test_node != NULL) {
		common_node = container_of(test_node, test_node_t, test_node);
		reference_node = amtree_find_key(&tt->reference_tree, test_node->key);

		if (last_test_link->next != &test_node->link) { errors++; break; }
		if (last_test_link != test_node->link.prev) { errors++; break; }
		last_test_link = &test_node->link;

		if (reference_node == NULL) { errors++; break; }
		if (common_node != container_of(reference_node, test_node_t, reference_node)) { errors++; break; }
		if (test_node->key <= last_key && test_node->key != 0 && last_key != 0)  { errors++; break; }
		target--;
		last_key = test_node->key;
		test_node = amitree_larger(&tt->test_tree, test_node);
	}

	if (last_test_link->next != &tt->test_tree.ordered_list) errors++;
	if (last_test_link != tt->test_tree.ordered_list.prev) errors++;
	if (target > 0) errors++;

	if (errors > 0) {
		printf("amitree validation failed\n");
		fflush(stdout);
		abort();
	}

	return errors;
}


int test_tree_validate(test_tree_t* tt)
{
	uint64_t errors = 0;

	ASSERT(tt != NULL);

	if (amtree_count(&tt->reference_tree) != amitree_count(&tt->test_tree)) errors++;
	if (amtree_root(&tt->reference_tree) == NULL) errors += (amitree_root(&tt->test_tree) != NULL);
	if (amtree_count(&tt->reference_tree) > 0)
		errors += test_tree_validate_nodes(amtree_root(&tt->reference_tree), amitree_root(&tt->test_tree));
	errors += test_tree_validate_order(tt);

	if (errors > 0) {
		printf("amitree validation failed\n");
		fflush(stdout);
		abort();
	}

	return errors;
}

int main()
{
	test_tree_t tt;
	test_node_t elements[MAX_ELEMENTS];
	uint64_t count_in = 0;
	uint64_t count_out = MAX_ELEMENTS;
	uint64_t reps = UINT32_MAX;
	uint64_t tmp;
	amitree_node_t spare;
	amitree_node_t* test_res;
	amtree_node_t* ref_res;
	amtime_t start = amtime_now();
	double randrat;
	int index;
	int in_idx;
	int out_idx;

	srand(start);
	memset(elements, 0, sizeof(elements));
	for (reps = 0; reps < MAX_ELEMENTS; reps++)
		elements[reps].key = reps;

	amtree_init(&tt.reference_tree);
	amitree_init(&tt.test_tree);

	printf("libam testing of amitree_t starting");
	fflush(stdout);

	for (reps = 0; reps < REPS_MAX; reps++) {
		test_tree_validate(&tt);

		if ((reps & 0x1FFFFF) == 0) {
			printf(".");
			fflush(stdout);
		}

		randrat = ((double)rand()) / ((double)RAND_MAX);

		index = rand() % MAX_ELEMENTS;
		test_res = amitree_find_key(&tt.test_tree, elements[index].key);
		ASSERT(!elements[index].is_in || test_res == &elements[index].test_node);
		ASSERT(elements[index].is_in || test_res == NULL);

		if (count_in == 0 || randrat < 0.4) { /* TEST ADD */
			if (elements[index].is_in) {
				// On the first index, check double insert
				memset(&spare, 0, sizeof(spare));
				spare.key = elements[index].key;
				test_res = amitree_insert(&tt.test_tree, &spare);
				ASSERT(test_res == &elements[index].test_node);
			}

			while (elements[index].is_in)
				index = (index + 1) % MAX_ELEMENTS;

			elements[index].reference_node.key = elements[index].key;
			ref_res = amtree_insert(&tt.reference_tree, &elements[index].reference_node);
			ASSERT(ref_res == NULL);

			elements[index].test_node.key = elements[index].key;
			test_res = amitree_insert(&tt.test_tree, &elements[index].test_node);
			ASSERT(test_res == NULL);

			elements[index].is_in = 1;
			count_in++;
			count_out--;
			//printf("element %lu added in, new count %u\n", elements[index].key, amtree_count(&tt.reference_tree));
			continue;
		}

		if (count_out == 0 || randrat < 0.82) {  /* TEST DELETE */
			while (!elements[index].is_in)
				index = (index + 1) % MAX_ELEMENTS;

			amtree_delete(&tt.reference_tree, &elements[index].reference_node);
			amitree_delete(&tt.test_tree, &elements[index].test_node);

			elements[index].is_in = 0;
			count_out++;
			count_in--;
			//printf("element %lu taken out, new count %u\n", elements[index].key, amtree_count(&tt.reference_tree));
			continue;
		}

		// TEST SWAP - At this point we're guaranteed to have elements in and out of the trees

		in_idx = -1;
		out_idx = -1;
		while (in_idx < 0 || out_idx < 0) {
			if (in_idx < 0 && elements[index].is_in)
				in_idx = index;
			if (out_idx < 0 && !elements[index].is_in)
				out_idx = index;
			index = (index + 1) % MAX_ELEMENTS;
		}

		/* Doing three-way swap */
		amtree_swap(&tt.reference_tree, &elements[in_idx].reference_node, &elements[out_idx].reference_node);
		amitree_swap(&tt.test_tree, &elements[in_idx].test_node, &elements[out_idx].test_node);
		elements[in_idx].is_in = 0;
		elements[out_idx].is_in = 1;

		tmp = elements[in_idx].key;
		elements[in_idx].key = elements[out_idx].key;
		elements[out_idx].key = tmp;

		//printf("element %d taken out, swapped for elemen %d that was added in it's place\n", in_idx, out_idx);
	}

	test_tree_validate(&tt);

	printf("\nlibam testing of amitree_t done successfully (%.2lf seconds)!\n", ((double)(amtime_now() - start)) / ((double)AMTIME_SEC));
	return 0;
}
