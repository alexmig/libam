#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#include "libam/libam_strhash.h"

#include "libam/libam_types.h"
#include "libam/libam_time.h"
#include "libam/libam_hash.h"
#include "libam/libam_log.h"

/* hash MUST use LIBAM_STRHASH_FLAG_DUP_KEYS */
/* constraint UINT64_MAX == off */
static void hash_fill(amstrhash_t* hash, uint64_t num, uint64_t constraint)
{
	char key[16];
	char formated_key[64];
	uint64_t i;
	uint64_t capacity;
	amrc_t rc;

	capacity = amstrhash_get_capacity(hash);

	srand(amtime_now());

	while (num > 0) {
		for (i = 0; i < sizeof(key); i++)
			key[i] = random() & 0xFF;
		key[sizeof(key) - 1] = '\0';

		/* Verify contraint */
		uint64_t hv = amshash(key, NULL);
		if (constraint != UINT64_MAX && (hv % capacity) != constraint)
			continue;

		amlog_hex(key, sizeof(key), formated_key, sizeof(formated_key));

		rc = amstrhash_insert(hash, (char*)key, (void*)num, NULL);
		if (rc == AMRC_SUCCESS)
			num--;
	}
}

static amrc_t check_percent_undergrowth()
{
	amstrhash_t* hash;
	amstrhash_attr_t attr;

	attr.bucket_threashold = UINT64_MAX;
	attr.percent_threashold = 50;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(16, LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);
	assert(amstrhash_get_capacity(hash) == 16);
	assert(amstrhash_get_size(hash) == 0);

	hash_fill(hash, 7, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 7);
	assert(amstrhash_get_capacity(hash) == 16);

	hash_fill(hash, 1, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 8);
	assert(amstrhash_get_capacity(hash) == 32);

	hash_fill(hash, 7, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 15);
	assert(amstrhash_get_capacity(hash) == 32);

	hash_fill(hash, 1, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 16);
	assert(amstrhash_get_capacity(hash) == 64);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_percent_overgrowth()
{
	amstrhash_t* hash;
	amstrhash_attr_t attr;

	attr.bucket_threashold = UINT64_MAX;
	attr.percent_threashold = 150;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(16, LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);
	assert(amstrhash_get_capacity(hash) == 16);
	assert(amstrhash_get_size(hash) == 0);

	hash_fill(hash, 23, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 23);
	assert(amstrhash_get_capacity(hash) == 16);

	hash_fill(hash, 1, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 24);
	assert(amstrhash_get_capacity(hash) == 32);

	hash_fill(hash, 23, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 47);
	assert(amstrhash_get_capacity(hash) == 32);

	hash_fill(hash, 1, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 48);
	assert(amstrhash_get_capacity(hash) == 64);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_bucket_growth()
{
	amstrhash_t* hash;
	amstrhash_attr_t attr;

	attr.bucket_threashold = 3;
	attr.percent_threashold = 200;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);

	hash_fill(hash, 2, 0);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 2);

	hash_fill(hash, 1, 0);
	assert(amstrhash_get_capacity(hash) == 16);
	assert(amstrhash_get_size(hash) == 3);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_no_growth()
{
	amstrhash_t* hash;
	amstrhash_attr_t attr;

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_FIXED_SIZE | LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);

	hash_fill(hash, 2, 0);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 2);

	hash_fill(hash, 1, 0);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 3);

	hash_fill(hash, 17, UINT64_MAX);
	assert(amstrhash_get_size(hash) == 20);
	assert(amstrhash_get_capacity(hash) == 8);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static uint64_t del_cb_count;
static const char* del_cb_last_key;
static void* del_cb_last_value;

static void on_delete_callback_init()
{
	del_cb_count = 0;
	if (del_cb_last_key != NULL)
		free((char*)del_cb_last_key);
	del_cb_last_key = NULL;
	del_cb_last_value = NULL;
}

static void on_delete_callback(const char* key, void* value)
{
	del_cb_count++;
	if (del_cb_last_key != NULL)
		free((char*)del_cb_last_key);
	del_cb_last_key = strdup(key);
	assert(del_cb_last_key != NULL);
	del_cb_last_value = value;
}

static amrc_t check_overwrite()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = on_delete_callback;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NO_FREE_CB | LIBAM_STRHASH_FLAG_OVERWRITE | LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(ent == NULL);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(del_cb_count == 0);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	rc = amstrhash_insert(hash, (char*)key, (void*)2, NULL);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 1);
	assert(del_cb_last_value == (void*)1);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)2);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 1);
	assert(del_cb_last_value == (void*)1);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_no_overwrite()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amstrhash_entry_t* old_ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = on_delete_callback;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NO_FREE_CB | LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(ent == NULL);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(del_cb_count == 0);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	old_ent = NULL;
	rc = amstrhash_insert(hash, (char*)key, (void*)2, &old_ent);
	assert(rc == AMRC_ERROR);
	assert(old_ent == ent);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_free_cb()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = on_delete_callback;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(ent == NULL);

	hash_fill(hash, 5, UINT64_MAX);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 5);
	assert(del_cb_count == 0);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 6);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 6);
	assert(del_cb_count == 0);

	rc = amstrhash_remove_key(hash, key);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 5);
	assert(del_cb_count == 1);
	assert(strcmp(key, del_cb_last_key) == 0);
	assert(del_cb_last_value == (void*)1);

	amstrhash_term(hash);
	assert(del_cb_count == 6);

	return AMRC_SUCCESS;
}

static amrc_t check_no_free_cb()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = on_delete_callback;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NO_FREE_CB | LIBAM_STRHASH_FLAG_DUP_KEYS, &attr);
	assert(hash);

	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 0);
	assert(ent == NULL);

	hash_fill(hash, 5, UINT64_MAX);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 5);
	assert(del_cb_count == 0);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 6);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_get_ent_key(ent)) == 0);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 6);
	assert(del_cb_count == 0);

	rc = amstrhash_remove_key(hash, key);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 5);
	assert(del_cb_count == 1);
	assert(strcmp(key, del_cb_last_key) == 0);
	assert(del_cb_last_value == (void*)1);

	amstrhash_term(hash);
	assert(del_cb_count == 1);

	return AMRC_SUCCESS;
}

static amrc_t check_no_dup_keys()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_OVERWRITE, &attr);
	assert(hash);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)1);

	rc = amstrhash_insert(hash, (char*)key, (void*)2, NULL);
	assert(rc == AMRC_SUCCESS);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)2);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_insert_find_delete()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 5;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NONE, &attr);
	assert(hash);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_size(hash) == 1);

	rc = amstrhash_remove(hash, ent);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_size(hash) == 0);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_size(hash) == 0);

	rc = amstrhash_remove(hash, NULL);
	assert(rc == AMRC_ERROR);
	assert(amstrhash_get_size(hash) == 0);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_insert_find_delete_key()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NONE, &attr);
	assert(hash);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_size(hash) == 1);

	rc = amstrhash_remove_key(hash, key);
	assert(rc == AMRC_SUCCESS);
	assert(amstrhash_get_size(hash) == 0);

	ent = amstrhash_find(hash, key);
	assert(ent == NULL);
	assert(amstrhash_get_size(hash) == 0);

	ent = amstrhash_find(hash, "foobar");
	assert(ent == NULL);
	assert(amstrhash_get_size(hash) == 0);

	rc = amstrhash_remove_key(hash, "dummy2");
	assert(rc == AMRC_ERROR);
	assert(amstrhash_get_size(hash) == 0);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_value_replace()
{
	static const char* key = "dummy key";
	amstrhash_t* hash;
	amstrhash_attr_t attr;
	amstrhash_entry_t* ent;
	amrc_t rc;

	on_delete_callback_init();

	attr.bucket_threashold = 3;
	attr.percent_threashold = 100;
	attr.free_size = 0;
	attr.on_delete = NULL;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_NONE, &attr);
	assert(hash);

	rc = amstrhash_insert(hash, (char*)key, (void*)1, NULL);
	assert(rc == AMRC_SUCCESS);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)1);
	assert(amstrhash_get_size(hash) == 1);

	amstrhash_set_ent_value(ent, (void*)2);
	assert(amstrhash_get_ent_value(ent) == (void*)2);
	assert(amstrhash_get_size(hash) == 1);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(amstrhash_get_ent_key(ent) == key);
	assert(amstrhash_get_ent_value(ent) == (void*)2);
	assert(amstrhash_get_size(hash) == 1);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}


static amrc_t check_functional_tests()
{
	/* Check basic operations */
	check_insert_find_delete();
	check_insert_find_delete_key();
	check_value_replace();

	/* Check flags function */
	check_percent_undergrowth();
	check_percent_overgrowth();
	check_bucket_growth();
	check_no_growth();
	check_overwrite();
	check_no_overwrite();
	check_free_cb();
	check_no_free_cb();
	check_no_dup_keys();
	return AMRC_SUCCESS;
}

/* Multi threading test tools */

enum {
	MAX_THREADS = 5,
	MIN_THREADS = 1,
	OBJECTS_PER_LIST = 1024,
	OPERATIONS = OBJECTS_PER_LIST * 8192,
};

typedef enum state {
	IN,
	OUT,
} state_t;

typedef struct object {
	char key[16];
	uint64_t value;
	volatile uint64_t target_state;
	volatile uint64_t actual_state;
} object_t;

typedef struct obj_list {
	object_t objs[OBJECTS_PER_LIST];
	volatile uint64_t objs_in;
} obj_list_t;

typedef struct thread_ctx {
	obj_list_t* shared;
	obj_list_t exclusive;
	volatile int status;
	int id;
	pthread_t thread;
	amstrhash_t* hash;
} thread_ctx_t;

typedef void* (*run_t)(void*);

typedef struct threadlist {
	run_t thread_func;
	thread_ctx_t ents[MAX_THREADS];
	uint64_t size;
	obj_list_t shared;
} threadlist_t;

static void init_objlist(obj_list_t* olist)
{
	uint64_t i;
	long unsigned int rc;

	memset(olist, 0, sizeof(*olist));
	for (i = 0; i < OBJECTS_PER_LIST; i++) {
		rc = snprintf(olist->objs[i].key, sizeof(olist->objs[i].key), "%p", &olist->objs[i]);
		assert(rc < sizeof(olist->objs[i].key)); /* Ensure uinique keys */
		olist->objs[i].value = ((uint64_t)olist) + i;
		olist->objs[i].target_state = OUT;
		olist->objs[i].actual_state = OUT;
	}
}

static void init_threadlist(threadlist_t* tlist, amstrhash_t* hash, run_t thread_func, uint64_t num_threads)
{
	uint64_t i;
	thread_ctx_t* ctx;

	memset(tlist, 0, sizeof(*tlist));
	tlist->thread_func = thread_func;

	if (num_threads > MAX_THREADS)
		num_threads = MAX_THREADS;
	tlist->size = num_threads;
	init_objlist(&tlist->shared);

	for (i = 0; i < num_threads; i++) {
		ctx = &tlist->ents[i];

		ctx->id = i;
		ctx->status = 0;
		ctx->hash = hash;
		ctx->shared = &tlist->shared;
		init_objlist(&ctx->exclusive);
	}
}

static void start_threadlist(threadlist_t* list)
{
	uint64_t i;
	int rc;
	thread_ctx_t* ent;

	for (i = 0; i < list->size; i++) {
		ent = &list->ents[i];
		ent->status = 1;
		rc = pthread_create(&ent->thread, NULL, list->thread_func, ent);
		if (rc != 0) {
			printf("Failed to start thread %lu (list %p) with %d (errno %d)\n", i, list, rc, errno);
			ent->status = 0;
			exit(-1);
		}
	}
}

static void stop_threadlist(threadlist_t* list)
{
	uint64_t i;
	int rc;

	for (i = 0; i < list->size; i++) {
		rc = pthread_join(list->ents[i].thread, NULL);
		if (rc != 0) {
			printf("Failed to stop thread %lu (list %p) with %d (errno %d)\n", i, list, rc, errno);
			exit(-1);
		}
	}
}

static void* thread_func(void* data)
{
	thread_ctx_t* ctx = data;
	uint64_t ops_done = 0;
	unsigned int rand_state = (unsigned int)amtime_now();
	uint64_t obj_index;
	object_t* obj;
	obj_list_t* list;
	uint64_t cur_target_state = -1;
	uint64_t new_target_state = -1;
	amrc_t rc;

	while (ops_done < OPERATIONS) {
		if (rand_r(&rand_state) % 2)
			list = ctx->shared;
		else
			list = &ctx->exclusive;

		obj_index = rand_r(&rand_state) % OBJECTS_PER_LIST;
		obj = &list->objs[obj_index];

		rc = AMRC_ERROR;
		while (rc != AMRC_SUCCESS) {
			do {
				cur_target_state = obj->target_state;
				if (cur_target_state == IN)
					new_target_state = OUT;
				else
					new_target_state = IN;
			} while (!amsync_swap(&obj->target_state, cur_target_state, new_target_state));

			if (new_target_state == IN)
				rc = amstrhash_insert(ctx->hash, obj->key, obj, NULL);
			else
				rc = amstrhash_remove_key(ctx->hash, obj->key);
			/* Maybe other thread failed by other thread. Check & loop if needed */
		}

		while (!amsync_swap(&obj->actual_state, cur_target_state, new_target_state));

		/* Done with object, do accounting */
		if (new_target_state == IN)
			amsync_inc(&list->objs_in);
		else
			amsync_dec(&list->objs_in);

		ops_done++;
	}

	ctx->status = 0;
	return NULL;
}

static void validate_objlist(obj_list_t* olist, amstrhash_t* hash)
{
	uint64_t i;
	object_t* obj;
	amstrhash_entry_t* ent;
	uint64_t in = 0;

	for (i = 0; i < OBJECTS_PER_LIST; i++) {
		obj = &olist->objs[i];

		ent = amstrhash_find(hash, obj->key);
		if (obj->actual_state == IN) {
			assert(ent != NULL);
			assert(strcmp(amstrhash_get_ent_key(ent), obj->key) == 0);
			assert(amstrhash_get_ent_value(ent) == obj->key);
			in++;
		}
		else {
			assert(ent == NULL);
		}
	}

	assert(in == olist->objs_in);
}

static void validate(threadlist_t* tlist, amstrhash_t* hash)
{
	uint64_t i;
	uint64_t expected = 0;

	validate_objlist(&tlist->shared, hash);
	expected = tlist->shared.objs_in;
	for (i = 0; i < tlist->size; i++) {
		validate_objlist(&tlist->ents[i].exclusive, hash);
		expected += tlist->ents[i].exclusive.objs_in;
	}

	assert(amstrhash_get_size(hash) == expected);
}

static void run_threaded_test(uint64_t thread_num)
{
	amstrhash_t* hash;
	threadlist_t tlist;

	hash = amstrhash_init(8, LIBAM_STRHASH_FLAG_USE_LOCK, NULL);

	init_threadlist(&tlist, hash, thread_func, thread_num);
	start_threadlist(&tlist);
	stop_threadlist(&tlist);
	validate(&tlist, hash);

	amstrhash_term(hash);
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
	amtime_t start;
	uint64_t i;

	start = amtime_now();
	srandom(start);

	printf("libam testing of amstrhash starting.");
	fflush(stdout);

	check_functional_tests();

	for (i = 0; i < 5; i++) {
		run_threaded_test(1);
		run_threaded_test(2);
		run_threaded_test(3);
		run_threaded_test(5);
		printf(".");
		fflush(stdout);
	}

	printf("\nlibam testing of amstrhash done successfully (%.2lf seconds)!\n", ((double)amtime_now() - start) / ((double)AMTIME_SEC));
	return 0;
}
