#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "libam.h"
#include "libam/libam_pool.h"

/**
 * TYPES & CONSTANTS
 * -----------------------------------------------------------------------------
 */

enum {
	MAX_THREADS = 15,
	MIN_THREADS = 1,
	OBJECTS = 256,
	MIN_SIZE = 8,
	MAX_SIZE = 1024,
	ROUNDS = 10240,
};

#define static_pool_name "Sequential_test_pool"

typedef enum status {
	ALLOCATED,
	FREED,
} status_t;

typedef enum action {
	ALLOCATE,
	FREE,
	REALLOCATE,
	GET_SIZE,
	TEST,
	TERM
} action_t;

typedef struct {
	void* ptr;
	uint64_t object_id;
	uint64_t size;
	uint64_t seq;
	status_t status;
	uint8_t data[MAX_SIZE];
} object_t;

/**
 * GLOBALS
 * -----------------------------------------------------------------------------
 */

static object_t all_objects[OBJECTS];
static volatile uint64_t free_objects = OBJECTS;
static volatile uint64_t used_objects = 0;
static volatile uint64_t seq = 0;
static volatile uint64_t opseq = 0;
static volatile uint64_t allocated_size = 0;


/**
 * CODE
 * -----------------------------------------------------------------------------
 */

#define UNUSED_SYM(x) (void)(x)

#define pcase(x) case x: return #x;
#define pdef(x) default: return "Unknown "#x;
/*static const char* act_2str(action_t act)
{
	switch (act) {
	pcase(ALLOCATE);
	pcase(FREE);
	pcase(REALLOCATE);
	pcase(GET_SIZE);
	pcase(TEST);
	pcase(TERM);
	pdef(action_t);
	}
}*/

static ampool_t* prep() {
	uint64_t i;

	for (i = 0; i < OBJECTS; i++) {
		all_objects[i].ptr = NULL;
		all_objects[i].object_id = i + 1;
		all_objects[i].seq = 0;
		all_objects[i].size = 0;
		all_objects[i].status = FREED;
		memset(all_objects[i].data, 0, sizeof(all_objects[i].data));
	}

	free_objects = OBJECTS;
	used_objects = 0;
	seq = 1;

	allocated_size = 0;
	used_objects = 0;

	return ampool_pool_alloc_flags_named(NULL, AMPOOL_VALIDATE_ON_FREE, static_pool_name);
}

static action_t action_randomizer() {
	double r = rand();
	r = r / ((double)RAND_MAX);

	/* 00.1% */ if (r <= 0.001) return TERM;
	/* 00.9% */ if (r <= 0.010) return TEST;
	/* 09.0% */ if (r <= 0.100) return GET_SIZE;
	/* 10.0% */	if (r <= 0.200) return REALLOCATE;
	/* 38.0% */	if (r <= 0.580) return ALLOCATE;
	/* 42.0% */ return FREE;
}

static inline int random_index() {
	_Static_assert(RAND_MAX > OBJECTS, "Must be greater");
	return rand() % OBJECTS;
}

static inline int random_size() {
	double r = rand();
	r = r / ((double)RAND_MAX);
	r *= r;
	return (r * (MAX_SIZE - MIN_SIZE)) + MIN_SIZE;
}

static void func_allocate(ampool_t* pool)
{
	uint64_t rnd;
	object_t* obj;
	uint64_t i;

	if (free_objects == 0)
		return;

	rnd = random_index();
	/* Find first allocatable object */
	for (i = 0; i < OBJECTS; i++) {
		obj = &all_objects[(rnd + i) % OBJECTS];
		if (obj->status == ALLOCATED)
			continue;
		amsync_dec(&free_objects);
		obj->seq = amsync_inc(&seq);
		obj->size = random_size();
		//printf("%lu-%lu:\tALC: Seq %lu, allocating object %lu with size %lu\n", used_objects, free_objects, obj->seq, obj->object_id, obj->size);
		obj->ptr = pool->alloc(pool, obj->size, (char*)obj->object_id);
		assert(obj->ptr);

		obj->status = ALLOCATED;
		amsync_add(&allocated_size, obj->size);

		memset(obj->data, 0x33, sizeof(obj->data));
		memcpy(obj->data, &obj->seq, sizeof(obj->seq));
		memcpy(&obj->data[obj->size - sizeof(obj->seq)], &obj->seq, sizeof(obj->seq));
		memcpy(obj->ptr, obj->data, obj->size);
		amsync_inc(&used_objects);

		return;
	}

	abort();
}

static void func_free(ampool_t* pool)
{
	uint64_t rnd;
	object_t* obj;
	uint64_t i;

	if (used_objects == 0)
		return;

	rnd = random_index();
	for (i = 0; i < OBJECTS; i++) {
		obj = &all_objects[(rnd + i) % OBJECTS];
		if (obj->status == FREED)
			continue;
		amsync_dec(&used_objects);
		assert(memcmp(obj->data, obj->ptr, obj->size) == 0);
		//printf("%lu-%lu:\tFRE: Seq %lu, freeing object %lu with size %lu\n", used_objects, free_objects, obj->seq, obj->object_id, obj->size);
		pool->free(pool, obj->ptr, obj->size);
		obj->ptr = NULL;
		amsync_sub(&allocated_size, obj->size);
		obj->status = FREED;
		amsync_inc(&free_objects);

		return;
	}

	abort();
}

static void func_realloc(ampool_t* pool)
{
	uint64_t rnd;
	object_t* obj;
	uint64_t i;
	uint64_t new_size;

	if (used_objects == 0)
		return;

	rnd = random_index();
	for (i = 0; i < OBJECTS; i++) {
		obj = &all_objects[(rnd + i) % OBJECTS];
		if (obj->status == FREED)
			continue;
		new_size = random_size();
		//printf("%lu-%lu:\tREA: Seq %lu, reallocing object %lu from size %lu to %lu\n", used_objects, free_objects, obj->seq, obj->object_id, obj->size, new_size);

		assert(memcmp(obj->data, obj->ptr, obj->size) == 0);
		obj->ptr = pool->realloc(pool, obj->ptr, obj->size, new_size, (char*)obj->object_id);
		assert(obj->ptr);
		assert(memcmp(obj->data, obj->ptr, _MIN(obj->size, new_size)) == 0);
		amsync_sub(&allocated_size, obj->size);
		amsync_add(&allocated_size, new_size);
		obj->size = new_size;

		obj->seq = amsync_inc(&seq);
		memset(obj->data, 0x33, sizeof(obj->data));
		memcpy(obj->data, &obj->seq, sizeof(obj->seq));
		memcpy(&obj->data[obj->size - sizeof(obj->seq)], &obj->seq, sizeof(obj->seq));
		memcpy(obj->ptr, obj->data, obj->size);

		return;
	}

	abort();
}

static void func_getsize(ampool_t* pool)
{
	uint64_t size = pool->get_size(pool);
	assert(size == allocated_size);
	UNUSED_SYM(size); /* In release build, asserts are gone, pleaving rc unused */
}

static uint8_t obj_stats[OBJECTS];
static uint64_t stat_size;
static uint64_t stat_elems;
static ampool_t* stat_pool;
static amrc_t fnuc_test_cb(const ampool_elem_diag_t* di, UNUSED void* user_data)
{
	uint64_t obj_id = (uint64_t)di->elem_name;
	object_t* obj = &all_objects[obj_id - 1];

	assert(di->pool == stat_pool);
	assert(strcmp(di->pool_name, static_pool_name) == 0);

	assert(obj->object_id == obj_id);
	assert(obj->ptr == di->elem);
	assert(obj->size == di->elem_size);
	assert(obj->status == ALLOCATED);
	assert(memcmp(obj->data, di->elem, obj->size) == 0);

	stat_elems ++;
	stat_size += obj->size;
	obj_stats[obj_id - 1]++;

	return AMRC_SUCCESS;
}

static void func_test(ampool_t* pool)
{
	object_t* obj;
	uint64_t i;
	uint64_t verify_size = 0;
	uint64_t verify_elems = 0;

	memset(obj_stats, 0, sizeof(obj_stats));
	stat_size = 0;
	stat_elems = 0;
	stat_pool = pool;

	ampool_elem_diag(pool, fnuc_test_cb, NULL, NULL);

	for (i = 0; i < OBJECTS; i++) {
		obj = &all_objects[i];

		assert(obj_stats[i] < 2);
		if (obj_stats[i] == 0)
			assert(obj->status == FREED);
		else {
			assert(obj->status == ALLOCATED);
			verify_size += obj->size;
			verify_elems ++;
		}
	}

	assert(verify_size == stat_size);
	assert(verify_elems == stat_elems);
	assert(verify_size == allocated_size);
	assert(verify_elems == used_objects);
}

static void func_term(ampool_t* pool)
{
	func_test(pool);
	pool->pool_free(pool);
}

static void run_until_done(uint64_t seed) {
	ampool_t* pool;
	action_t act;

	if (seed == 0)
		seed = amtime_now();
	//printf("Seed: %lu\n", seed);
	fflush(stdout);
	srand(seed);

	pool = prep();
	func_test(pool);

	while (1) {
		act = action_randomizer();
		//printf("Action: %lu %s\n", amsync_inc(&opseq) + 1, act_2str(act));
		switch (act) {
		case ALLOCATE: 		func_allocate(pool); break;
		case FREE:			func_free(pool); break;
		case REALLOCATE:	func_realloc(pool); break;
		case GET_SIZE:		func_getsize(pool); break;
		case TEST:			func_test(pool); break;
		case TERM:			func_term(pool); return;
		}
	}
}

/* Status: We currently have very rudimentary functional tests
 * TODO: hierarchical add/delete pools
 * TODO: multi-threaded alloc/free
 * TODO: Verify overflow protections are working
 * TODO: Verify overflow data
 */
int main(UNUSED int argc, UNUSED const char** argv)
{
	uint64_t i;
	amtime_t start;

	ampool_init();

	printf("libam testing of ampool_t starting.");
	fflush(stdout);
	start = amtime_now();
	for (i = 0; i < ROUNDS; i++) {
		run_until_done(0);
		if ((i & 0xFFF) == 0)
			printf(".");
		fflush(stdout);
	}

	printf("\nlibam testing of amstack_t done successfully (%.2lf seconds)!\n", ((double)amtime_now() - start) / ((double)AMTIME_SEC));
}
