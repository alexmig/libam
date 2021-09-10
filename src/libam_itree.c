#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "libam/libam_itree.h"
#include "libam/libam_log.h"

// Returns compare if equal, otherwise proper child (Can be NULL).
static inline amitree_node_t* amitree_cmp(const amitree_node_t* targ,
		amitree_node_t* compare)
{
	if (targ->key > compare->key)
		return compare->right;
	if (targ->key < compare->key)
		return compare->left;
	return compare;
}

// Lookup

amitree_node_t* amitree_find(const amitree_t* tree, const amitree_node_t* target)
{
	amitree_node_t* cur = tree->root;
	amitree_node_t* next;

	while (cur != NULL) {
		next = amitree_cmp(target, cur);
		if (next == cur)
			return cur;
		cur = next;
	}
	return NULL;
}

amitree_node_t* amitree_find_key(const amitree_t* tree, const uint64_t key)
{
	amitree_node_t node = { .key = key };
	return amitree_find(tree, &node);
}

// Basic ops

static inline amitree_node_t** amitree_which_child(amitree_node_t* parent,
		const amitree_node_t* child)
{
	if (parent->left == child)
		return &parent->left;
	return &parent->right;
}

// Assume existence of left child
static void amitree_rotate_right(amitree_t* t, amitree_node_t* n, amitree_node_t* cl)
{
	amitree_node_t* p = n->parent;
	amitree_node_t* glr = cl->right;

	// link parent-child
	if (p == NULL)
		t->root = cl;
	else
		*amitree_which_child(p, n) = cl;
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
static void amitree_rotate_left(amitree_t* t, amitree_node_t* n, amitree_node_t* cr)
{
	amitree_node_t* p = n->parent;
	amitree_node_t* grl = cr->left;

	// link parent-child
	if (p == NULL)
		t->root = cr;
	else
		*amitree_which_child(p, n) = cr;
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

static void amitree_rotate_right_left(amitree_t* t, amitree_node_t* n, amitree_node_t* cr)
{
	amitree_node_t* p = n->parent;
	amitree_node_t* grl = cr->left;
	amitree_node_t* ggrlr = grl->right;
	amitree_node_t* ggrll = grl->left;

	// Re-linking
	if (p == NULL)
		t->root = grl;
	else
		*amitree_which_child(p, n) = grl;
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

static void amitree_rotate_left_right(amitree_t* t, amitree_node_t* n, amitree_node_t* cl)
{
	amitree_node_t* p = n->parent;
	amitree_node_t* glr = cl->right;
	amitree_node_t* gglrl = glr->left;
	amitree_node_t* gglrr = glr->right;

	// Re-linking
	if (p == NULL)
		t->root = glr;
	else
		*amitree_which_child(p, n) = glr;
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

static void amitree_balance_add(amitree_t* t, amitree_node_t* n)
{
	amitree_node_t* p;

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
				return amitree_rotate_left(t, p, n);
			return amitree_rotate_right_left(t, p, n);
		case -2:
			if (n->balance == -1)
				return amitree_rotate_right(t, p, n);
			return amitree_rotate_left_right(t, p, n);
		default:
			AMLOG_PLAIN(dlog, "impossible balance for tree key %lx - %d\n",
					p->key, p->balance);
			amlog_flush(dlog);
			abort();
		}
	}
}

// Returns NULL on success / Existing node on duplicate
amitree_node_t* amitree_insert(amitree_t* t, amitree_node_t* n)
{
	amitree_node_t* parent;
	amitree_node_t* next = t->root;
	amlink_t* parent_link;

	// If empty
	if (t->root == NULL) {
		t->root = n;
		t->count = 1;
		n->parent = NULL;
		n->left = NULL;
		n->right = NULL;
		n->balance = 0;
		amlist_add(&t->ordered_list, &n->link);
		return NULL;
	}

	do {
		parent = next;
		parent_link = &parent->link;
		next = amitree_cmp(n, parent);
		if (next == parent)
			return parent;
	} while (next != NULL);

	n->left = NULL;
	n->right = NULL;
	n->balance = 0;
	n->parent = parent;
	if (n->key < parent->key) {
		parent->left = n;
		amlist_add_tail(parent_link, &n->link);
	}
	else {
		parent->right = n;
		amlist_add(parent_link, &n->link);
	}
	t->count += 1;

	amitree_balance_add(t, n);
	return NULL;
}

// Node's balance has already been adjusted
static void amitree_balance_del(amitree_t* t, amitree_node_t* parent)
{
	amitree_node_t* next;
	amitree_node_t* c;

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
				amitree_rotate_left(t, parent, c);
				if (c->balance != 0)
					return;
				parent = c;
				break;
			}
			amitree_rotate_right_left(t, parent, c);
			parent = parent->parent;
			break;
		case -2:
			c = parent->left;
			if (c->balance != +1) {
				amitree_rotate_right(t, parent, c);
				if (c->balance != 0)
					return;
				parent = c;
				break;
			}
			amitree_rotate_left_right(t, parent, c);
			parent = parent->parent;
			break;
		default:
			AMLOG_PLAIN(dlog, "impossible balance for tree key %lx - %d\n",
					parent->key, parent->balance);
			amlog_flush(dlog);
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
void amitree_delete(amitree_t* tree, amitree_node_t* node)
{
	amitree_node_t* p = node->parent;
	amitree_node_t* cr = node->right;
	amitree_node_t* cl = node->left;
	amitree_node_t* min;
	amitree_node_t* min_p;

	tree->count -= 1;
	amlist_del(&node->link);
	memset(&node->link, 0, sizeof(node->link));

	if (cr != NULL && cl != NULL) {
		if (cr->left == NULL) {
			cr->parent = p;
			if (p == NULL)
				tree->root = cr;
			else
				*amitree_which_child(p, node) = cr;

			cl->parent = cr;
			cr->left = cl;
			cr->balance = node->balance - 1;
			amitree_balance_del(tree, cr);
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
			*amitree_which_child(p, node) = min;
		cl->parent = min;
		min->left = cl;
		cr->parent = min;
		min->right = cr;

		amitree_balance_del(tree, min_p);
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
	amitree_balance_del(tree, p);
}

amitree_node_t* amitree_delete_key(amitree_t* tree, const uint64_t key)
{
	amitree_node_t* targ = amitree_find_key(tree, key);
	if (targ == NULL)
		return NULL;
	amitree_delete(tree, targ);
	return targ;
}

void amitree_swap(amitree_t* tree, amitree_node_t* to_remove, amitree_node_t* to_insert)
{
	memcpy(to_insert, to_remove, sizeof(amitree_node_t));
	memset(to_remove, 0, sizeof(amitree_node_t));
	to_insert->link.next->prev = &to_insert->link;
	to_insert->link.prev->next = &to_insert->link;

	if (to_insert->parent == NULL)
		tree->root = to_insert;
	else
		*amitree_which_child(to_insert->parent, to_remove) = to_insert;
	if (to_insert->left)
		to_insert->left->parent = to_insert;
	if (to_insert->right)
		to_insert->right->parent = to_insert;
}

amrc_t amitree_init(amitree_t* tree)
{
	memset((tree), 0, sizeof(*tree));
	amlist_init(&tree->ordered_list);
	return AMRC_SUCCESS;
}
