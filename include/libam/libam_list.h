/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2010 Francisco Jerez <currojerez@riseup.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _LIBAM_LIST_H_
#define _LIBAM_LIST_H_

#include "libam_types.h"

struct amlist_head {
	struct amlist_head *next;
	struct amlist_head *prev;
};

typedef struct amlist_head amlist_t; // To be used as designating anchor
typedef struct amlist_head amlink_t; // To be used as designating member

static inline void amlist_init(amlist_t *list)
{
	list->next = list;
	list->prev = list;
}

static inline void __amlist_add(struct amlist_head *entry,
		struct amlist_head *prev, struct amlist_head *next)
{
	next->prev = entry;
	entry->next = next;
	entry->prev = prev;
	prev->next = entry;
}

/**
 * Insert a new element after the given list head. The new element does not
 * need to be initialised as empty list.
 * The list changes from:
 *      head → some element → ...
 * to
 *      head → new element → older element → ...
 *
 * Example:
 * struct foo *newfoo = malloc(...);
 * list_add(&bar->list_of_foos, &newfoo->entry);
 *
 * @param entry The new element to prepend to the list.
 * @param head The existing list.
 */
static inline void amlist_add(amlist_t *head, amlink_t *entry)
{
	__amlist_add(entry, head, head->next);
}

/**
 * Append a new element to the end of the list given with this list head.
 *
 * The list changes from:
 *      head → some element → ... → lastelement
 * to
 *      head → some element → ... → lastelement → new element
 *
 * Example:
 * struct foo *newfoo = malloc(...);
 * list_add_tail(&bar->list_of_foos, &newfoo->entry);
 *
 * @param entry The new element to prepend to the list.
 * @param head The existing list.
 */
static inline void amlist_add_tail(amlist_t *head, amlink_t *entry)
{
	__amlist_add(entry, head->prev, head);
}

static inline void __amlist_del(struct amlist_head *prev,
		struct amlist_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * Remove the element from the list it is in. Using this function will reset
 * the pointers to/from this element so it is removed from the list. It does
 * NOT free the element itself or manipulate it otherwise.
 *
 * Using list_del on a pure list head (like in the example at the top of
 * this file) will NOT remove the first element from
 * the list but rather reset the list as empty list.
 *
 * Example:
 * list_del(&foo->entry);
 *
 * @param entry The element to remove.
 */
static inline void amlist_del(amlink_t *entry)
{
	__amlist_del(entry->prev, entry->next);
}

/**
 * Check if the list is empty.
 *
 * Example:
 * list_empty(&bar->list_of_foos);
 *
 * @return True if the list contains one or more elements or False otherwise.
 */
static inline ambool_t amlist_empty(amlist_t *head)
{
	return head->next == head;
}

/**
 * Returns a pointer to the container of this list element.
 *
 * Example:
 * struct foo* f;
 * f = container_of(&foo->entry, struct foo, entry);
 * assert(f == foo);
 *
 * @param ptr Pointer to the struct amlist_head.
 * @param type Data type of the list element.
 * @param member Member name of the struct amlist_head field in the list element.
 * @return A pointer to the data struct containing the list head.
 */
#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - (char *) &((type *)0)->member))
#endif

/**
 * Alias of container_of
 */
#define amlist_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * Retrieve the first list entry for the given list pointer.
 *
 * Example:
 * struct foo *first;
 * first = list_first_entry(&bar->list_of_foos, struct foo, list_of_foos);
 *
 * @param ptr The list head
 * @param type Data type of the list element to retrieve
 * @param member Member name of the struct amlist_head field in the list element.
 * @return A pointer to the first list element.
 */
#define amlist_first_entry(ptr, type, member) \
    amlist_entry((ptr)->next, type, member)

/**
 * Retrieve the last list entry for the given listpointer.
 *
 * Example:
 * struct foo *first;
 * first = list_last_entry(&bar->list_of_foos, struct foo, list_of_foos);
 *
 * @param ptr The list head
 * @param type Data type of the list element to retrieve
 * @param member Member name of the struct amlist_head field in the list element.
 * @return A pointer to the last list element.
 */
#define amlist_last_entry(ptr, type, member) \
    amlist_entry((ptr)->prev, type, member)

#define __container_of(ptr, sample, member)				\
    (void *)container_of((ptr), typeof(*(sample)), member)

/**
 * Loop through the list given by head and set pos to struct in the list.
 *
 * Example:
 * struct foo *iterator;
 * list_for_each_entry(iterator, &bar->list_of_foos, entry) {
 *      [modify iterator]
 * }
 *
 * This macro is not safe for node deletion. Use list_for_each_entry_safe
 * instead.
 *
 * @param pos Iterator variable of the type of the list elements.
 * @param head List head
 * @param member Member name of the struct amlist_head in the list elements.
 *
 */
#define amlist_for_each_entry(pos, head, member) \
    for (pos = __container_of((head)->next, pos, member); \
	 &pos->member != (head); \
	 pos = __container_of(pos->member.next, pos, member))

#endif
