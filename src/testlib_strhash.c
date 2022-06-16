#include <assert.h>
#include <errno.h>
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
		printf("Inserting key '%s', value %p, bucket %lu/%lu\n", formated_key, (void*)num, hv % amstrhash_get_capacity(hash), amstrhash_get_capacity(hash)); fflush(stdout);

		rc = amstrhash_insert(hash, (char*)key, (void*)num, NULL);
		if (rc == AMRC_SUCCESS)
			num--;
		printf("Inserted!\n"); fflush(stdout);
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
	assert(strcmp(key, amstrhash_ent_key(ent)) == 0);
	assert(amstrhash_ent_value(ent) == (void*)1);
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
	assert(strcmp(key, amstrhash_ent_key(ent)) == 0);
	assert(amstrhash_ent_value(ent) == (void*)2);
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
	assert(strcmp(key, amstrhash_ent_key(ent)) == 0);
	assert(amstrhash_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	old_ent = NULL;
	rc = amstrhash_insert(hash, (char*)key, (void*)2, &old_ent);
	assert(rc == AMRC_ERROR);
	assert(old_ent == ent);
	assert(strcmp(key, amstrhash_ent_key(ent)) == 0);
	assert(amstrhash_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	ent = amstrhash_find(hash, key);
	assert(ent != NULL);
	assert(strcmp(key, amstrhash_ent_key(ent)) == 0);
	assert(amstrhash_ent_value(ent) == (void*)1);
	assert(amstrhash_get_capacity(hash) == 8);
	assert(amstrhash_get_size(hash) == 1);
	assert(del_cb_count == 0);

	amstrhash_term(hash);

	return AMRC_SUCCESS;
}

static amrc_t check_functional_tests()
{
	check_percent_undergrowth();
	check_percent_overgrowth();
	check_bucket_growth();
	check_no_growth();
	check_overwrite();
	check_no_overwrite();

	return AMRC_SUCCESS;
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

	start = amtime_now();

	printf("libam testing of amstrhash starting.");
	fflush(stdout);

	check_functional_tests();

	printf("\nlibam testing of amstrhash done successfully (%.2lf seconds)!\n", ((double)amtime_now() - start) / ((double)AMTIME_SEC));
	return 0;
}
