#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "libam/libam_tree.h"
#include "libam/libam_log.h"

#if 0
#include <stdio.h>
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
#endif

// Returns compare if equal, otherwise proper child (Can be NULL).
static inline amtree_node_t* amtree_cmp(const amtree_node_t* targ,
		amtree_node_t* compare)
{
	if (targ->key > compare->key)
		return compare->right;
	if (targ->key < compare->key)
		return compare->left;
	return compare;
}

// Lookup

amtree_node_t* amtree_find(const amtree_t* tree, const amtree_node_t* target)
{
	amtree_node_t* cur = tree->root;
	amtree_node_t* next;

	while (cur != NULL) {
		next = amtree_cmp(target, cur);
		if (next == cur)
			return cur;
		cur = next;
	}
	return NULL;
}

amtree_node_t* amtree_find_key(const amtree_t* tree, const uint64_t key)
{
	amtree_node_t node = { .key = key };
	return amtree_find(tree, &node);
}

// Basic ops

static inline amtree_node_t** amtree_which_child(amtree_node_t* parent,
		const amtree_node_t* child)
{
	if (parent->left == child)
		return &parent->left;
	return &parent->right;
}

// Assume existence of left child
static void amtree_rotate_right(amtree_t* t, amtree_node_t* n, amtree_node_t* cl)
{
	amtree_node_t* p = n->parent;
	amtree_node_t* glr = cl->right;

	// link parent-child
	if (p == NULL)
		t->root = cl;
	else
		*amtree_which_child(p, n) = cl;
	cl->parent = p;

	// Link node-grandchild
	n->left = glr;
	if (glr != NULL)
		glr->parent = n;

	// Link (reverse) child-node
	n->parent = cl;
	cl->right = n;

	// Balances
	if (cl->balance == 0) {
		n->balance = -1;
		cl->balance = +1;
	}
	else { // -1
		n->balance = 0;
		cl->balance = 0;
	}
}

// Assume existence of left child
static void amtree_rotate_left(amtree_t* t, amtree_node_t* n, amtree_node_t* cr)
{
	amtree_node_t* p = n->parent;
	amtree_node_t* grl = cr->left;

	// link parent-child
	if (p == NULL)
		t->root = cr;
	else
		*amtree_which_child(p, n) = cr;
	cr->parent = p;

	// Link node-grandchild
	n->right = grl;
	if (grl != NULL)
		grl->parent = n;

	// Link (reverse) child-node
	n->parent = cr;
	cr->left = n;

	// Balances
	if (cr->balance == 0) {
		n->balance = +1;
		cr->balance = -1;
	}
	else { // +1
		n->balance = 0;
		cr->balance = 0;
	}
}

static void amtree_rotate_right_left(amtree_t* t, amtree_node_t* n, amtree_node_t* cr)
{
	amtree_node_t* p = n->parent;
	amtree_node_t* grl = cr->left;
	amtree_node_t* ggrlr = grl->right;
	amtree_node_t* ggrll = grl->left;

	// Re-linking
	if (p == NULL)
		t->root = grl;
	else
		*amtree_which_child(p, n) = grl;
	grl->parent = p;

	cr->left = ggrlr;
	if (ggrlr != NULL)
		ggrlr->parent = cr;

	n->right = ggrll;
	if (ggrll != NULL)
		ggrll->parent = n;

	grl->right = cr;
	cr->parent = grl;

	grl->left = n;
	n->parent = grl;

	// Balance
	if (grl->balance > 0) {
		n->balance = -1;
		cr->balance = 0;
	}
	else if (grl->balance == 0) {
		n->balance = 0;
		cr->balance = 0;
	}
	else { // -1
		n->balance = 0;
		cr->balance = +1;
	}
	grl->balance = 0;
}

static void amtree_rotate_left_right(amtree_t* t, amtree_node_t* n, amtree_node_t* cl)
{
	amtree_node_t* p = n->parent;
	amtree_node_t* glr = cl->right;
	amtree_node_t* gglrl = glr->left;
	amtree_node_t* gglrr = glr->right;

	// Re-linking
	if (p == NULL)
		t->root = glr;
	else
		*amtree_which_child(p, n) = glr;
	glr->parent = p;

	cl->right = gglrl;
	if (gglrl != NULL)
		gglrl->parent = cl;

	n->left = gglrr;
	if (gglrr != NULL)
		gglrr->parent = n;

	glr->left = cl;
	cl->parent = glr;

	glr->right = n;
	n->parent = glr;

	// Balance
	if (glr->balance < 0) {
		n->balance = +1;
		cl->balance = 0;
	}
	else if (glr->balance == 0) {
		n->balance = 0;
		cl->balance = 0;
	}
	else { // +1
		n->balance = 0;
		cl->balance = -1;
	}
	glr->balance = 0;
}

static void amtree_balance_add(amtree_t* t, amtree_node_t* n)
{
	amtree_node_t* p;

	for (p = n->parent; p != NULL; n = n->parent, p = p->parent) {
		if (p->left == n)
			p->balance -= 1;
		else
			p->balance += 1;

		switch (p->balance) {
		case 0:
			return;
		case +1:
		case -1:
			break;
		case +2:
			if (n->balance == +1)
				return amtree_rotate_left(t, p, n);
			return amtree_rotate_right_left(t, p, n);
		case -2:
			if (n->balance == -1)
				return amtree_rotate_right(t, p, n);
			return amtree_rotate_left_right(t, p, n);
		default:
			/* impossible balance for tree key p->key - p->balance */
			abort();
		}
	}
}

// Returns NULL on success / Existing node on duplicate
amtree_node_t* amtree_insert(amtree_t* t, amtree_node_t* n)
{
	amtree_node_t* parent;
	amtree_node_t* next = t->root;

	// If empty
	if (t->root == NULL) {
		t->root = n;
		t->count = 1;
		n->parent = NULL;
		n->left = NULL;
		n->right = NULL;
		n->balance = 0;
		return NULL;
	}

	do {
		parent = next;
		next = amtree_cmp(n, parent);
		if (next == parent)
			return parent;
	} while (next != NULL);

	n->left = NULL;
	n->right = NULL;
	n->balance = 0;
	n->parent = parent;
	if (n->key < parent->key)
		parent->left = n;
	else
		parent->right = n;
	t->count += 1;

	amtree_balance_add(t, n);
	return NULL;
}

// Node's balance has already been adjusted
static void amtree_balance_del(amtree_t* t, amtree_node_t* parent)
{
	amtree_node_t* next;
	amtree_node_t* c;

	do {
		switch (parent->balance) {
		case 0:
			break; // Continue loop
		case +1:
		case -1:
			return;
		case +2:
			c = parent->right;
			if (c->balance != -1) {
				amtree_rotate_left(t, parent, c);
				if (c->balance != 0)
					return;
				parent = c;
				break;
			}
			amtree_rotate_right_left(t, parent, c);
			parent = parent->parent;
			break;
		case -2:
			c = parent->left;
			if (c->balance != +1) {
				amtree_rotate_right(t, parent, c);
				if (c->balance != 0)
					return;
				parent = c;
				break;
			}
			amtree_rotate_left_right(t, parent, c);
			parent = parent->parent;
			break;
		default:
			/* impossible balance for tree key parent->key - parent->balance */
			abort();
		}

		next = parent->parent;
		if (next == NULL)
			return;

		if (next->left == parent)
			next->balance += 1;
		else
			next->balance -= 1;
		parent = next;
	} while (1);
}

// Returns node / pointer to existing node
void amtree_delete(amtree_t* tree, amtree_node_t* node)
{
	amtree_node_t* p = node->parent;
	amtree_node_t* cr = node->right;
	amtree_node_t* cl = node->left;
	amtree_node_t* min;
	amtree_node_t* min_p;

	tree->count -= 1;

	if (cr != NULL && cl != NULL) {
		if (cr->left == NULL) {
			cr->parent = p;
			if (p == NULL)
				tree->root = cr;
			else
				*amtree_which_child(p, node) = cr;

			cl->parent = cr;
			cr->left = cl;
			cr->balance = node->balance - 1;
			amtree_balance_del(tree, cr);
			return;
		}

		for (min = cr; min->left != NULL; min = min->left)
			;
		min_p = min->parent;
		min_p->left = min->right;
		if (min->right != NULL)
			min->right->parent = min_p;
		min_p->balance += 1;

		// Replace min and n
		min->balance = node->balance;
		min->parent = p;
		if (p == NULL)
			tree->root = min;
		else
			*amtree_which_child(p, node) = min;
		cl->parent = min;
		min->left = cl;
		cr->parent = min;
		min->right = cr;

		amtree_balance_del(tree, min_p);
		return;
	}

	// At least one child missing

	if (cr != NULL) {
		cl = cr;
		cr = NULL;
	}

	if (cl != NULL)
		cl->parent = p;

	if (p == NULL) {
		tree->root = cl;
		return;
	}

	// There is a parent
	if (p->left == node) {
		p->left = cl;
		p->balance += 1;
	}
	else {
		p->right = cl;
		p->balance -= 1;
	}
	amtree_balance_del(tree, p);
}

amtree_node_t* amtree_delete_key(amtree_t* tree, const uint64_t key)
{
	amtree_node_t* targ = amtree_find_key(tree, key);
	if (targ == NULL)
		return NULL;
	amtree_delete(tree, targ);
	return targ;
}

void amtree_swap(amtree_t* tree, amtree_node_t* to_remove, amtree_node_t* to_insert)
{
	memcpy(to_insert, to_remove, sizeof(amtree_node_t));
	memset(to_remove, 0, sizeof(amtree_node_t));

	if (to_insert->parent == NULL)
		tree->root = to_insert;
	else
		*amtree_which_child(to_insert->parent, to_remove) = to_insert;
	if (to_insert->left)
		to_insert->left->parent = to_insert;
	if (to_insert->right)
		to_insert->right->parent = to_insert;
}
