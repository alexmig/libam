#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#ifdef NDEBUG
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); abort(); }} while(0)
#else
#include <assert.h>
#endif

#include "libam/libam_time.h"
#include "libam/libam_atomic.h"
#include "libam/libam_lstack.h"

/**
 * TYPES & CONSTANTS
 * -----------------------------------------------------------------------------
 */

enum {
	MAX_THREADS = 15,
	MIN_THREADS = 1,
	STACK_SIZE = 8,
	WRITE_OBJECTS = MAX_THREADS * STACK_SIZE * 4096, // total amount of objects to write. Each reader thread must accomodate all possible objects
	RECORD_LENGTH = 15,
};

typedef struct {
	amlstack_node_t node;
	uint64_t object_id;
	uint8_t record_index;
	uint8_t record[RECORD_LENGTH];
} object_t;

typedef enum threadlist_id {
	TL_INVALID = 0,
	TL_READERS,
	TL_WRITERS,
	TL_MEDDLERS,
	TL_MAX
} threadlist_id_t;

typedef struct thread_ctx {
	volatile int status;
	volatile int progress;
	threadlist_id_t lid;
	int id;
	pthread_t thread;
	uint64_t target_capacity;
	object_t** objects;
} thread_ctx_t;

typedef void* (*run_t)(void*);

typedef struct {
	threadlist_id_t lid;
	run_t thread_func;
	uint64_t size;
	thread_ctx_t ents[MAX_THREADS];
	object_t** objlist;
} threadlist_t;

/**
 * GLOBALS
 * -----------------------------------------------------------------------------
 */

static volatile uint64_t signal_go = 0;
static volatile uint64_t signal_stop = 0;

static volatile uint64_t writes = 0;
static volatile uint64_t reads = 0;
static volatile uint64_t meddles = 0;

static threadlist_t* tl = NULL;
static amlstack_t stack;
static object_t* all_objects = NULL;

/**
 * CODE
 * -----------------------------------------------------------------------------
 */

void* reader_thread_func(void* data)
{
	thread_ctx_t* ent = (thread_ctx_t*)data;
	object_t* obj;
	amlstack_node_t* node;
	uint8_t opid;
	uint64_t ops_done = 0;

	assert((ent->id & 0xF) == ent->id);
	assert((ent->lid & 0xF) == ent->lid);
	opid = ((ent->lid & 0xF) << 4) | (ent->id & 0xF);

	while (!signal_go); // Bustwait for signal
	while (ops_done < ent->target_capacity) {
		do {
			node = amlstack_pop(&stack);
		} while (node == NULL);
		obj = container_of(node, object_t, node);
		obj->record[obj->record_index] = opid;
		ent->objects[ops_done] = obj;
		ops_done++;
		ent->progress = ops_done;
		amsync_inc(&reads);
	}

	ent->status = 0;
	return NULL;
}

void* writer_thread_func(void* data)
{
	thread_ctx_t* ent = (thread_ctx_t*)data;
	object_t* obj;
	uint8_t opid;
	uint64_t ops_done = 0;

	assert((ent->id & 0xF) == ent->id);
	assert((ent->lid & 0xF) == ent->lid);
	opid = ((ent->lid & 0xF) << 4) | (ent->id & 0xF);

	while (!signal_go); // Bustwait for signal
	while (ops_done < ent->target_capacity) {
		obj = ent->objects[ops_done];
		ent->objects[ops_done] = NULL;
		obj->record[obj->record_index] = opid;
		obj->record_index = (obj->record_index + 1) % RECORD_LENGTH;
		amlstack_push(&stack, &obj->node);
		ops_done++;
		ent->progress = ops_done;
		amsync_inc(&writes);
	}

	ent->status = 0;
	return NULL;
}

void* meddler_thread_func(void* data)
{
	thread_ctx_t* ent = (thread_ctx_t*)data;
	object_t* obj;
	amlstack_node_t* node;
	uint64_t ops_done = 0;
	uint8_t opid;

	assert((ent->id & 0xF) == ent->id);
	assert((ent->lid & 0xF) == ent->lid);
	opid = ((ent->lid & 0xF) << 4) | (ent->id & 0xF);

	while (!signal_go); // Bustwait for signal
	while (!signal_stop) {
		obj = NULL;
		do {
			node = amlstack_pop(&stack);
		} while (node == NULL && !signal_stop);
		if (signal_stop)
			break;
		obj = container_of(node, object_t, node);
		obj->record[obj->record_index] = opid;
		obj->record_index = (obj->record_index + 1) % RECORD_LENGTH;
		if (signal_stop) {
			printf("ERROR: Meddler thread %d caught with object id %lu\n", ent->id, obj->object_id);
			break;
		}
		amlstack_push(&stack, &obj->node);

		ops_done++;
		ent->progress = ops_done;
		amsync_inc(&meddles);
	}

	ent->status = 0;
	return NULL;
}
static void init_tl(threadlist_t* tl, threadlist_id_t type, run_t func)
{
	uint64_t i;

	tl->lid = type;
	tl->thread_func = func;
	tl->size = 0;
	if (tl->objlist != NULL)
		memset(tl->objlist, 0, sizeof(*tl->objlist) * WRITE_OBJECTS);
	for (i = 0; i < MAX_THREADS; i++) {
		tl->ents[i].status = 0;
		tl->ents[i].progress = 0;
		tl->ents[i].lid = type;
		assert((tl->ents[i].lid & 0xF) == tl->ents[i].lid);
		tl->ents[i].id = i + 1;
		assert((tl->ents[i].id & 0xF) == tl->ents[i].id);
		tl->ents[i].target_capacity = 0;
		tl->ents[i].objects = NULL;
	}
}

static void globals_term()
{
	fflush(stdout);
	amlstack_term(&stack);

	if (all_objects != NULL)
		free(all_objects);
	if (tl != NULL)
		free(tl);
	if (tl[TL_WRITERS].objlist != NULL)
		free(tl[TL_WRITERS].objlist);
	if (tl[TL_READERS].objlist != NULL)
		free(tl[TL_READERS].objlist);
}

static amrc_t globals_init()
{
	tl = NULL;
	all_objects = NULL;

	amlstack_init(&stack);

	all_objects = malloc(sizeof(*all_objects) * WRITE_OBJECTS);
	if (all_objects == NULL) {
		printf("Failed to allocate objects\n");
		goto cleanup;
	}
	memset(all_objects, 0, sizeof(*all_objects) * WRITE_OBJECTS);

	tl = malloc(sizeof(*tl) * TL_MAX);
	if (tl == NULL) {
		printf("Failed to allocate thread list\n");
		goto cleanup;
	}
	memset(tl, 0, sizeof(*tl) * TL_MAX);

	tl[TL_WRITERS].objlist = malloc(sizeof(*tl[TL_WRITERS].objlist) * WRITE_OBJECTS);
	if (tl[TL_WRITERS].objlist == NULL) {
		printf("Failed to allocate writers list\n");
		goto cleanup;
	}

	tl[TL_READERS].objlist = malloc(sizeof(*tl[TL_READERS].objlist) * WRITE_OBJECTS);
	if (tl[TL_READERS].objlist == NULL) {
		printf("Failed to allocate writers list\n");
		goto cleanup;
	}

	init_tl(&tl[TL_READERS], TL_READERS, reader_thread_func);
	init_tl(&tl[TL_WRITERS], TL_WRITERS, writer_thread_func);
	init_tl(&tl[TL_MEDDLERS], TL_MEDDLERS, meddler_thread_func);

	return AMRC_SUCCESS;

cleanup:
	globals_term();
	return AMRC_ERROR;
}



static void prep_objects()
{
	uint64_t i;

	memset(all_objects, 0, sizeof(*all_objects) * WRITE_OBJECTS);
	for (i = 0; i < WRITE_OBJECTS; i++) {
		all_objects[i].object_id = i + 1;
		all_objects[i].record_index = 0;
	}
}

static void prep_generic_tl(threadlist_t* tl, uint64_t size)
{
	uint64_t i;
	uint64_t capacity = WRITE_OBJECTS;
	uint64_t per_thread_capacity = WRITE_OBJECTS / size;
	object_t** objects;

	memset(tl->objlist, 0, sizeof(*tl[TL_WRITERS].objlist) * WRITE_OBJECTS);

	tl->size = size;
	objects = tl->objlist;
	for (i = 0; i < size; i++) {
		tl->ents[i].status = 0;
		tl->ents[i].progress = 0;
		tl->ents[i].target_capacity = per_thread_capacity;
		tl->ents[i].objects = objects;
		objects += per_thread_capacity;
		capacity -= per_thread_capacity;
	}
	if (capacity > 0)
		tl->ents[size - 1].target_capacity += capacity;
}

static void prep_readers(uint64_t size)
{
	prep_generic_tl(&tl[TL_READERS], size);
}

static void prep_writers(uint64_t size)
{
	uint64_t i;

	prep_generic_tl(&tl[TL_WRITERS], size);

	for (i = 0; i < WRITE_OBJECTS; i++) {
		tl[TL_WRITERS].objlist[i] = &all_objects[i];
	}
}

static void prep_meddlers(uint64_t size)
{
	uint64_t i;

	tl[TL_MEDDLERS].size = size;
	for (i = 0; i < size; i++) {
		tl[TL_MEDDLERS].ents[i].status = 0;
		tl[TL_MEDDLERS].ents[i].progress = 0;
	}
}

static void start_list(threadlist_t* list)
{
	uint64_t i;
	int rc;
	thread_ctx_t* ent;

	for (i = 0; i < list->size; i++) {
		ent = &list->ents[i];
		ent->status = 1;
		rc = pthread_create(&ent->thread, NULL, list->thread_func, ent);
		if (rc != 0) {
			printf("Failed to start thread %d-%lu (list %p) with %d (errno %d)\n", list->lid, i, list, rc, errno);
			ent->status = 0;
			exit(-1);
		}
	}
}

static void stop_list(threadlist_t* list)
{
	uint64_t i;
	int rc;

	for (i = 0; i < list->size; i++) {
		rc = pthread_join(list->ents[i].thread, NULL);
		if (rc != 0) {
			printf("Failed to stop thread %d-%lu (list %p) with %d (errno %d)\n", list->lid, i, list, rc, errno);
			exit(-1);
		}
	}
}

typedef struct opcounts {
	uint64_t op[TL_MAX];
} opcounts_t;

static char optype_2str(threadlist_id_t optype)
{
	switch (optype) {
	case TL_READERS: return 'R';
	case TL_WRITERS: return 'W';
	case TL_MEDDLERS: return 'M';
	default: return 'I';
	}
}

static void validate_obj_history(object_t* obj, opcounts_t* opcnt, ambool_t print)
{
	uint64_t i;

	if (print)
		printf("Object %lu history: ", obj->object_id);

	for (i = 0; i < RECORD_LENGTH; i++) {
		uint8_t op = obj->record[i];
		uint8_t optype = (op >> 4) & 0xF;
		uint8_t thread_id = op & 0xF;
		if (op == 0)
			continue;
		if (optype <= TL_INVALID || optype >= TL_MAX)
			opcnt->op[TL_INVALID]++;
		else
			opcnt->op[optype]++;

		if (print)
			printf("%c%u ", optype_2str(optype), thread_id);
	}
	if (print)
		printf("\n");
}

uint64_t print_all_objects()
{
	typedef struct error_flags {
		uint8_t error_finding:1;
		uint8_t error_writing:1;
		uint8_t error_opcount:1;
		uint8_t error_stack:1;
		uint8_t has_parent:1;
		uint8_t is_root:1;
	} error_flags_t;

	char err_buffer[64];
	error_flags_t errors[WRITE_OBJECTS];
	object_t* obj;
	uint64_t i;
	uint64_t errors_total = 0;
	opcounts_t tmpcnt;

	memset(errors, 0, sizeof(errors));
	for (i = 0; i < WRITE_OBJECTS; i++) {
		errors[i].error_finding = 1;
	}

	for (i = 0; i < WRITE_OBJECTS; i++) {
		if (tl[TL_WRITERS].objlist[i] != NULL) {
			obj = tl[TL_WRITERS].objlist[i];
			if (obj != NULL) {
				errors[obj->object_id - 1].error_writing = 1;
				errors[obj->object_id - 1].error_finding = 0;
			}
		}

		if (tl[TL_READERS].objlist[i] != NULL) {
			obj = tl[TL_READERS].objlist[i];
			if (obj != NULL) {
				errors[obj->object_id - 1].error_finding = 0;
			}
		}
	}

	for (i = 0; i < WRITE_OBJECTS; i++) {
		obj = &all_objects[i];
		assert(obj->object_id == i + 1);

		if (obj->node.next != NULL && !errors[i].has_parent) {
			errors[i].is_root = 1;
			while (obj->node.next != NULL) {
				errors[obj->object_id - 1].error_stack = 1;
				obj = container_of(obj->node.next, object_t, node);
				errors[obj->object_id - 1].has_parent = 1;
				errors[obj->object_id - 1].is_root = 0;
			}
		}
		obj = &all_objects[i];

		memset(&tmpcnt, 0, sizeof(tmpcnt));
		validate_obj_history(obj, &tmpcnt, am_false);
		if (tmpcnt.op[TL_READERS] != 1 || tmpcnt.op[TL_INVALID] != 0 || (
				tmpcnt.op[TL_WRITERS] != 1 && tmpcnt.op[TL_READERS] + tmpcnt.op[TL_MEDDLERS] != RECORD_LENGTH)) {
			errors[i].error_opcount = 1;
		}
	}

	for (i = 0; i < WRITE_OBJECTS; i++) {
		obj = &all_objects[i];
		assert(obj->object_id == i + 1);

		if (errors[i].error_finding || errors[i].error_writing || errors[i].error_opcount || errors[i].error_stack) {
			errors_total++;
			sprintf(err_buffer, "[%s %s %s %s]",
					(errors[i].error_finding ? "FINDING": ""),
					(errors[i].error_writing ? "WRITING": ""),
					(errors[i].error_opcount ? "OPCOUNT": ""),
					(errors[i].error_stack ? "STACK": ""));
			printf("ERROR: The following errors %s observed on obj %lu\n", err_buffer, obj->object_id);
			validate_obj_history(obj, &tmpcnt, am_true);
		}
	}

	/* Print chains */
	for (i = 0; i < WRITE_OBJECTS; i++) {
		if (!errors[i].is_root)
			continue;
		obj = &all_objects[i];
		while (1) {
			printf("%lu -> ", obj->object_id);
			if (obj->node.next == NULL)
				break;
			obj = container_of(obj->node.next, object_t, node);
		}
		printf("\n");
	}

	if (errors_total > 0)
		printf("ERROR: Total count %lu\n", errors_total);

	return errors_total;
}

#define valc(x, i, e) if (!(x)) { printf("Failed condition "#x" for i %lu\n", (i)); (e)++; fflush(stdout); abort(); }
static amrc_t validate()
{
	uint64_t i;
	uint64_t errors = 0;
	object_t* obj;
	opcounts_t opcnt;
	opcounts_t tmpcnt;
	amrc_t ret = AMRC_SUCCESS;

	if (print_all_objects())
		ret = AMRC_ERROR;

	memset(&opcnt, 0, sizeof(opcnt));

	for (i = 0; i < WRITE_OBJECTS; i++) {
		valc(tl[TL_WRITERS].objlist[i] == NULL, i, errors);
		obj = tl[TL_READERS].objlist[i];
		valc(obj != NULL, i, errors);
		if (obj == NULL) {
			ret = AMRC_ERROR;
			continue;
		}

		valc(obj->object_id != 0, i, errors);
		memset(&tmpcnt, 0, sizeof(tmpcnt));
		validate_obj_history(obj, &tmpcnt, 0);
		valc(tmpcnt.op[TL_INVALID] == 0, i, errors);
		valc(tmpcnt.op[TL_WRITERS] <= 1, i, errors); // Might be buried by meddling
		valc(tmpcnt.op[TL_READERS] == 1, i, errors);
		opcnt.op[TL_INVALID] += tmpcnt.op[TL_INVALID];
		opcnt.op[TL_READERS] += tmpcnt.op[TL_READERS];
		opcnt.op[TL_WRITERS] += tmpcnt.op[TL_WRITERS];
		opcnt.op[TL_MEDDLERS] += tmpcnt.op[TL_MEDDLERS];

		if (errors) {
			obj->object_id = i + 1;
			validate_obj_history(obj, &tmpcnt, 1);
			errors = 0;
			ret = AMRC_ERROR;
		}

		obj->object_id = 0; // Mark as read
	}

	// Make sure all objects were processed
	for (i = 0; i < WRITE_OBJECTS; i++) {
		valc(all_objects[i].object_id == 0, i, errors);
		if (errors) {
			obj->object_id = i + 1;
			validate_obj_history(obj, &tmpcnt, 1);
			errors = 0;
			ret = AMRC_ERROR;
		}
	}

	// Validate opcounts
	valc(opcnt.op[TL_INVALID] == 0, i, errors);
	valc(opcnt.op[TL_WRITERS] <= WRITE_OBJECTS, i, errors); // Might be buried by meddling
	valc(opcnt.op[TL_READERS] == WRITE_OBJECTS, i, errors);
	if (errors)
		ret = AMRC_ERROR;

	return ret;
}

static amrc_t run_single(int n_rds, int n_wrs, int n_mdl)
{
	if (n_rds < MIN_THREADS || n_rds > MAX_THREADS || n_wrs < MIN_THREADS || n_wrs > MAX_THREADS || n_mdl < 0 || n_mdl > MAX_THREADS) {
		printf("All thread numbers must range between %d-%d (M can be 0), received R %d, W %d, M %d\n",
				MIN_THREADS, MAX_THREADS, n_rds, n_wrs, n_mdl);
		return AMRC_ERROR;
	}

	prep_objects();
	prep_readers(n_rds);
	prep_writers(n_wrs);
	prep_meddlers(n_mdl);

	signal_go = 0;
	signal_stop = 0;

	start_list(&tl[TL_READERS]);
	start_list(&tl[TL_WRITERS]);
	start_list(&tl[TL_MEDDLERS]);

	//printf("Starting round %d, [R: %d, W: %d, B: %d]\n", i_round, readers.size, writers.size, bothers.size);
	//fflush(stdout);

	usleep(500);
	signal_go = 1;

	stop_list(&tl[TL_WRITERS]);
	stop_list(&tl[TL_READERS]);
	signal_stop = 1;
	stop_list(&tl[TL_MEDDLERS]);

	signal_go = 0;
	signal_stop = 0;

	assert(writes == WRITE_OBJECTS);
	writes = 0;
	assert(reads == WRITE_OBJECTS);
	reads = 0;
	meddles = 0;

	fflush(stdout);

	return validate();
}

static amrc_t run_meddlers(int rds, int wts)
{
	amrc_t rc = AMRC_SUCCESS;

	rc = rc || run_single(rds, wts, 0);
	rc = rc || run_single(rds, wts, 1);
	rc = rc || run_single(rds, wts, 5);

	return rc;
}

static amrc_t run_writers(int rds)
{
	amrc_t rc = AMRC_SUCCESS;

	rc = rc || run_meddlers(rds, 1);
	rc = rc || run_meddlers(rds, 5);
	rc = rc || run_meddlers(rds, 10);

	return rc;
}

static amrc_t run_readers()
{
	amrc_t rc = AMRC_SUCCESS;

	rc = rc || run_writers(1);
	rc = rc || run_writers(5);
	rc = rc || run_writers(10);

	return rc;
}

/* Basic idea - Have three groups of threads - Readers, writers, and meddlers
 * Writers simply deplete their pools of objects into the stack as fast as they can.
 * Meddlers take out an object from the stack, and put it back
 * Readers deplete the stack and store objects
 *
 * All writers combined will always write WRITE_OBJECTS objects to the stack, and all readers combined will always read as much.
 * Once done, the readers and writers would stop temselves. The meddlers will wait for the signal_stop to fire before shutting down.
 * The amount of meddling is varaible, and depends on contention and scheduling
 *
 * All actions are recorded in the objects themselves.
 * The stack is sized small, to get a lot of contention around the empty & full cases.
 * After a round is done, all object are counted and histories accounted for, to make sure everything lines up */
int main()
{
	amrc_t rc;
	uint64_t i;
	amtime_t start;

	rc = globals_init();
	if (rc != AMRC_SUCCESS)
		return -1;

	start = amtime_now();

	printf("libam testing of amlstack_t starting.");
	fflush(stdout);
	for (i = 0; i < 5; i++) {
		run_readers();
		printf(".");
		fflush(stdout);
	}

	printf("\nlibam testing of amlstack_t done successfully (%.2lf seconds)!\n", ((double)amtime_now() - start) / ((double)AMTIME_SEC));
	globals_term();
	return 0;
}
