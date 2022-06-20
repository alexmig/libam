#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "libam/libam_pool.h"
#include "libam/libam_replace.h"
#include "libam/libam_list.h"
#include "libam/libam_atomic.h"
#include "libam/libam_hash.h"
#include "libam/libam_time.h"

enum ampool_constants {
	AMPOOL_ALIGN_BITS = 4,
	AMPOOL_ALIGN = 1 << AMPOOL_ALIGN_BITS,
	AMPOOL_ALIGN_MASK = AMPOOL_ALIGN - 1,

	AMPOOL_STEP_COUNT = 32,
	AMPOOL_MAX_STEPPED = AMPOOL_ALIGN * AMPOOL_STEP_COUNT,

	AMPOOL_MAX_VALIDATE = 64, /* Maximum amount of data to validate for free ovjects */
	AMPOOL_MAX_VALIDATE_HALF = AMPOOL_MAX_VALIDATE / 2,
	AMPOOL_MAX_MEMSET = 1024,
};

#define UNUSED_SYM(x) (void)(x)

_Static_assert(is_power_of_two(AMPOOL_ALIGN), "AMPOOL_ALIGN Must be a power of two\n");

typedef struct ampool_chunk {
	amlink_t link;
	const char* name;
	uint32_t size;
	uint32_t magic;
	uint8_t data[0];
} ampool_chunk_t;

typedef struct ampool_bucket {
	pthread_mutex_t mutex;
	amlist_t used_list;
	amlist_t free_list;

	ampool_bucket_stats_t stats;
} ampool_bucket_t;

typedef struct ampool_ops {
	ampool_alloc_t alloc;
	ampool_realloc_t realloc;
	ampool_free_t free;
	ampool_get_size_t get_size;
	ampool_pool_free_t pool_free;
} ampool_ops_t;

typedef struct ampool_internal {
	/* Buckets */
	ampool_bucket_t steps[AMPOOL_STEP_COUNT];
	ampool_bucket_t oversized;

	volatile uint64_t size;
	volatile uint64_t element_count;

	/* Hierarchy tree - Locked behind a central hierarchy_mutex */
	/* Potential improvements: (1) rw_lock and (2) individual mutexes rather than one for the full hierarchy */
	struct ampool_internal* parent;
	amlink_t sibling_link;
	amlist_t children_list;

	/* Pool characteristics - Fixed for the life of the pool */
	ampool_flags_t flags;
	const char* name;
	ampool_t ops;
} ampool_internal_t;

#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)((char *)(ptr) - (char *) &((type *)0)->member))

#endif

typedef struct ampool_globals {
	uint32_t magic;
	amlist_t root_pools;
	pthread_mutex_t hierarchy_mutex;
} ampool_globals_t;

static ampool_globals_t globals;

static inline uint32_t align_size(uint32_t size)
{
	return (size + AMPOOL_ALIGN - 1) & ~AMPOOL_ALIGN_MASK;
}

static inline uint8_t chunk_magic_ptr(uint8_t* ptr, ambool_t data_magic)
{
	uint8_t ret = ((uint8_t*)(&globals.magic))[(uint64_t)ptr % sizeof(globals.magic)];
	if (data_magic)
		ret &= 0x0F;
	return ret;
}

static inline void chunk_magic_get_values(uint8_t* ptr, ambool_t is_data, ambool_t to_allocated, uint8_t* expectedp, uint8_t* desiredp)
{
	uint8_t expected;
	uint8_t desired;

	expected = chunk_magic_ptr(ptr, is_data);
	desired = ~expected;
	if (to_allocated) {
		expected = ~expected;
		desired = ~desired;
	}

	if (expectedp != NULL)
		*expectedp = expected;
	if (desiredp != NULL)
		*desiredp = desired;
}

static inline void chunk_magic_test_len(uint8_t* ptr, uint32_t remaining, ambool_t is_data, ambool_t to_allocated)
{
	uint8_t expected;

	while (remaining > 0) {
		chunk_magic_get_values(ptr, is_data, to_allocated, &expected, NULL);
		assert(*ptr == expected || (is_data && !to_allocated));
		ptr++;
		remaining--;
	}
}

static inline void chunk_magic_set_len(uint8_t* ptr, uint32_t remaining, ambool_t is_data, ambool_t to_allocated)
{
	uint8_t desired;

	while (remaining > 0) {
		chunk_magic_get_values(ptr, is_data, to_allocated, NULL, &desired);
		if (!is_data || !to_allocated)
			*ptr = desired;

		ptr++;
		remaining--;
	}
}

static void chunk_magic_test(ampool_chunk_t* chunk, uint32_t allocation_size, ambool_t to_allocated, ambool_t validate_data, ambool_t validate_overflow)
{
	uint8_t* ptr;
	uint32_t len;

	/* Always check header */
	assert(chunk->magic == globals.magic || to_allocated);
	assert(chunk->magic == ~globals.magic || !to_allocated);

	if (validate_data) { /* Check and flip data */
		len = (chunk->size + 1) / 2;
		len = _MIN(AMPOOL_MAX_VALIDATE_HALF, len);

		ptr = &chunk->data[0];
		chunk_magic_test_len(ptr, len, am_true, to_allocated);
		ptr = &chunk->data[chunk->size - len];
		chunk_magic_test_len(ptr, len, am_true, to_allocated);
	}

	if (validate_overflow) {
		ptr = &chunk->data[chunk->size];
		len = allocation_size - chunk->size;
		chunk_magic_test_len(ptr, len, am_false, to_allocated);
	}
}

static void chunk_magic_set(ampool_chunk_t* chunk, uint32_t allocation_size, ambool_t to_allocated, ambool_t set_data, ambool_t set_overflow)
{
	uint8_t* ptr;
	uint32_t len;

	/* Always set header */
	chunk->magic = (to_allocated ? globals.magic : ~globals.magic);

	if (set_data && !to_allocated) { /* Only set data when freeing a chunk */
		len = (chunk->size + 1) / 2;
		len = _MIN(AMPOOL_MAX_VALIDATE_HALF, len);

		ptr = &chunk->data[0];
		chunk_magic_set_len(ptr, len, am_true, to_allocated);
		ptr = &chunk->data[chunk->size - len];
		chunk_magic_set_len(ptr, len, am_true, to_allocated);
	}

	if (set_overflow) {
		ptr = &chunk->data[chunk->size];
		len = allocation_size - chunk->size;
		chunk_magic_set_len(ptr, len, am_false, to_allocated);
	}
}

static amrc_t bucket_init(ampool_bucket_t* bucket, uint32_t size)
{
	int rc;
	rc = pthread_mutex_init(&bucket->mutex, NULL);
	if (rc != 0)
		return AMRC_ERROR;

	amlist_init(&bucket->used_list);
	amlist_init(&bucket->free_list);
	bucket->stats.element_size = size;
	amstat_init(&bucket->stats.used_size_range);
	amstat_init(&bucket->stats.total_size_range);
	amstat_init(&bucket->stats.used_element_count_range);
	amstat_init(&bucket->stats.total_element_count_range);

	return AMRC_SUCCESS;
}

static void bucket_term_list(ampool_bucket_t* bucket, amlist_t* list, ambool_t validate, ambool_t is_used)
{
	ampool_chunk_t* chunk;
	uint32_t size_malloc;

	while (!amlist_empty(list)) {
		chunk = amlist_first_entry(list, ampool_chunk_t, link);
		amlist_del(&chunk->link);

		size_malloc = bucket->stats.element_size;
		if (size_malloc == 0)
			size_malloc = align_size(chunk->size);
		chunk_magic_test(chunk, size_malloc, !is_used, validate, validate);

		bucket->stats.total_size -= chunk->size;
		bucket->stats.total_element_count--;

		if (is_used) {
			bucket->stats.used_size -= chunk->size;
			bucket->stats.used_element_count--;
		}

		free(chunk);
	}
}

static void bucket_term(ampool_bucket_t* bucket, ambool_t validate)
{
	pthread_mutex_lock(&bucket->mutex);

	/* A choice to make here,
	 * either error out if bucket is still in use for tighter state machine.
	 * Or just kill the memory.
	 *
	 * Going with killing memory, user can always check size of pool before freeing to validate */

	bucket_term_list(bucket, &bucket->used_list, validate, am_true);
	bucket_term_list(bucket, &bucket->free_list, validate, am_false);

	pthread_mutex_unlock(&bucket->mutex);
	pthread_mutex_destroy(&bucket->mutex);
}

static ampool_chunk_t* bucket_alloc(ampool_bucket_t* bucket, uint32_t sub_size, const char* name, ambool_t validate)
{
	ampool_chunk_t* chunk;
	uint32_t size_malloc;

	size_malloc = bucket->stats.element_size;
	if (size_malloc == 0)
		size_malloc = align_size(sub_size);

	assert((size_malloc >= sub_size) && ((size_malloc - AMPOOL_ALIGN) < sub_size));
	assert(bucket->stats.element_size == 0 || sub_size <= AMPOOL_MAX_STEPPED);
	assert(bucket->stats.element_size > 0 || sub_size > AMPOOL_MAX_STEPPED);

	pthread_mutex_lock(&bucket->mutex);

	if (amlist_empty(&bucket->free_list)) {
		chunk = malloc(sizeof(*chunk) + size_malloc);
		if (chunk == NULL) {
			pthread_mutex_unlock(&bucket->mutex);
			return NULL;
		}

		bucket->stats.total_element_count++;
		amstat_upd(&bucket->stats.total_element_count_range, bucket->stats.total_element_count);
		bucket->stats.total_size += sub_size;
		amstat_upd(&bucket->stats.total_size_range, bucket->stats.total_size);
	}
	else {
		chunk = amlist_first_entry(&bucket->free_list, ampool_chunk_t, link);
		amlist_del(&chunk->link);

		chunk_magic_test(chunk, size_malloc, am_true, validate, validate);
	}

	amlist_add(&bucket->used_list, &chunk->link);

	bucket->stats.used_element_count++;
	amstat_upd(&bucket->stats.used_element_count_range, bucket->stats.used_element_count);
	bucket->stats.used_size += sub_size;
	amstat_upd(&bucket->stats.used_size_range, bucket->stats.used_size);

	pthread_mutex_unlock(&bucket->mutex);

	chunk->name = name;
	chunk->size = sub_size;
	chunk_magic_set(chunk, size_malloc, am_true, validate, validate);

	memset(chunk->data, 0, _MIN(sub_size, AMPOOL_MAX_MEMSET));

	return chunk;
}

static void bucket_free(ampool_bucket_t* bucket, void* ptr, ambool_t validate)
{
	ampool_chunk_t* chunk;
	uint32_t size_malloc;

	chunk = container_of(ptr, ampool_chunk_t, data[0]);

	size_malloc = bucket->stats.element_size;
	if (size_malloc == 0)
		size_malloc = align_size(chunk->size);

	assert((size_malloc >= chunk->size) && ((size_malloc - AMPOOL_ALIGN) < chunk->size));
	assert(bucket->stats.element_size == 0 || chunk->size <= AMPOOL_MAX_STEPPED);
	assert(bucket->stats.element_size > 0 || chunk->size > AMPOOL_MAX_STEPPED);

	chunk_magic_test(chunk, size_malloc, am_false, validate, validate);

	pthread_mutex_lock(&bucket->mutex);

	bucket->stats.used_element_count--;
	amstat_upd(&bucket->stats.used_element_count_range, bucket->stats.used_element_count);
	bucket->stats.used_size -= chunk->size;
	amstat_upd(&bucket->stats.used_size_range, bucket->stats.used_size);

	amlist_del(&chunk->link);

	if (bucket->stats.element_size > 0)
		amlist_add(&bucket->free_list, &chunk->link);

	pthread_mutex_unlock(&bucket->mutex);

	if (bucket->stats.element_size > 0)
		chunk_magic_set(chunk, size_malloc, am_false, validate, validate);
	else
		free(chunk);
}

static ampool_bucket_t* ampool_get_bucket(ampool_internal_t* pool, uint32_t size)
{
	assert((size & AMPOOL_ALIGN_MASK) == 0);

	if (size <= AMPOOL_MAX_STEPPED) {
		/* Search fixed buckets */
		return &pool->steps[(size >> AMPOOL_ALIGN_BITS) - 1];
	}
	return &pool->oversized;
}

static void* ampool_op_alloc(ampool_t* ops, uint32_t size, const char* name)
{
	ampool_internal_t* pool = container_of(ops, ampool_internal_t, ops);
	uint32_t aligned_size;
	ampool_bucket_t* bucket;
	ampool_chunk_t* chunk;

	if (size == 0)
		return NULL;

	aligned_size = align_size(size);

	bucket = ampool_get_bucket(pool, aligned_size);
	assert(bucket);

	chunk = bucket_alloc(bucket, size, name, !!(pool->flags & AMPOOL_VALIDATE_ON_FREE));
	if (chunk == NULL)
		return NULL;

	amsync_add(&pool->size, size);
	amsync_inc(&pool->element_count);
	return chunk->data;
}

static void	ampool_op_free(ampool_t* ops, void * ptr, uint32_t size)
{
	ampool_internal_t* pool = container_of(ops, ampool_internal_t, ops);
	uint32_t aligned_size;
	ampool_bucket_t* bucket;

	aligned_size = align_size(size);

	bucket = ampool_get_bucket(pool, aligned_size);
	assert(bucket);
	bucket_free(bucket, ptr, !!(pool->flags & AMPOOL_VALIDATE_ON_FREE));

	amsync_sub(&pool->size, size);
	amsync_dec(&pool->element_count);
}

static void* ampool_op_realloc(ampool_t* ops, void * ptr, uint32_t old_size, uint32_t new_size, const char* name)
{
	void* newptr;
	uint32_t min;

	min = _MIN(old_size, new_size);
	newptr = ops->alloc(ops, new_size, name);
	if (newptr != NULL) {
		memcpy(newptr, ptr, min);
		memset(((uint8_t*)newptr) + min, 0, new_size - min);
	}

	ops->free(ops, ptr, old_size);
	return newptr;
}

static uint64_t ampool_op_get_size(ampool_t* ops)
{
	ampool_internal_t* pool = container_of(ops, ampool_internal_t, ops);
	return pool->size;
}

/* Frees a pool without locking the hierarchy mutex. */
static void _ampool_pool_free(ampool_internal_t* pool)
{
	ampool_bucket_t* bucket;
	uint64_t i;
#ifdef DEBUG
	uint64_t deleted_size = 0;
	uint64_t deleted_count = 0;
#endif

	amlist_del(&pool->sibling_link); /* Sever top-down chain, now only tracable botom-up */

	/* Free all fixed buckets */
	for (i = 0; i < ARRAY_SIZE(pool->steps); i++) {
		bucket = &pool->steps[i];
#ifdef DEBUG
		deleted_size += bucket->stats.used_size;
		deleted_count += bucket->stats.used_element_count;
#endif
		bucket_term(bucket, !!(pool->flags & AMPOOL_VALIDATE_ON_FREE));
	}
	bucket = &pool->oversized;
#ifdef DEBUG
	deleted_size += bucket->stats.used_size;
	deleted_count += bucket->stats.used_element_count;
#endif
	bucket_term(bucket, !!(pool->flags & AMPOOL_VALIDATE_ON_FREE));

	/* Free all sub-pools */
	while (!amlist_empty(&pool->children_list)) {
		ampool_internal_t* subpool;
		subpool = amlist_first_entry(&pool->children_list, ampool_internal_t, sibling_link);
		_ampool_pool_free(subpool);
	}

#ifdef DEBUG
	assert(deleted_size == pool->size);
	assert(deleted_count == pool->element_count);
#endif

	free(pool);
}

static void	ampool_op_pool_free(ampool_t* ops)
{
	ampool_internal_t* pool = container_of(ops, ampool_internal_t, ops);
	pthread_mutex_lock(&globals.hierarchy_mutex);
	_ampool_pool_free(pool);
	pthread_mutex_unlock(&globals.hierarchy_mutex);
}

ampool_t* ampool_pool_alloc_flags_named(ampool_t* parent_ops, ampool_flags_t flags, const char* name)
{
	ampool_internal_t* parent = NULL;
	ampool_internal_t* pool = NULL;
	uint64_t i;
	amrc_t rc;

	if (parent_ops != NULL)
		parent = container_of(parent_ops, ampool_internal_t, ops);

	pool = malloc(sizeof(*pool));
	if (pool == NULL) {
		goto error;
	}
	memset(pool, 0, sizeof(*pool));
	amlist_init(&pool->children_list);
	pool->flags = flags;
	pool->name = name;
	pool->parent = parent;
	pool->ops.alloc = ampool_op_alloc;
	pool->ops.realloc = ampool_op_realloc;
	pool->ops.free = ampool_op_free;
	pool->ops.get_size = ampool_op_get_size;
	pool->ops.pool_free = ampool_op_pool_free;

	for (i = 0; i < ARRAY_SIZE(pool->steps); i++) {
		rc = bucket_init(&pool->steps[i], (i + 1) * AMPOOL_ALIGN);
		assert(rc == AMRC_SUCCESS);
	}
	rc = bucket_init(&pool->oversized, 0);
	assert(rc == AMRC_SUCCESS);
	UNUSED_SYM(rc); /* In release build, asserts are gone, pleaving rc unused */

	/* Add to hierarchy */
	pthread_mutex_lock(&globals.hierarchy_mutex);
	if (parent == NULL) {
		amlist_add(&globals.root_pools, &pool->sibling_link);
	}
	else {
		amlist_add(&parent->children_list, &pool->sibling_link);
	}
	pthread_mutex_unlock(&globals.hierarchy_mutex);

	return &pool->ops;

error:
	if (pool != NULL)
		free(pool);
	return NULL;
}

void ampool_init()
{
	int rc;
	uint64_t fold_me = amshash(__DATE__, NULL);

	amlist_init(&globals.root_pools);
	rc = pthread_mutex_init(&globals.hierarchy_mutex, NULL);
	assert(rc == 0);
	UNUSED_SYM(rc); /* In release build, asserts are gone, pleaving rc unused */

	fold_me ^= (fold_me >> 32);
	fold_me ^= (fold_me >> 16);
	globals.magic = ((fold_me & 0xFFFF) << 16) | (amtime_now() & 0xFFFF);
	//globals.magic = 0xFFFFFFFFU;
}

void ampool_internal_term()
{
	ampool_internal_t* pool;

	pthread_mutex_lock(&globals.hierarchy_mutex);
	while (!amlist_empty(&globals.root_pools)) {
		pool = amlist_first_entry(&globals.root_pools, ampool_internal_t, sibling_link);
		_ampool_pool_free(pool);
	}

	pthread_mutex_unlock(&globals.hierarchy_mutex);
}



static amrc_t _ampool_diag(ampool_internal_t* pool, ampool_internal_t* parent, ampool_diag_t* pd, ampool_diag_cb_t callback, void* user_data)
{
	amrc_t rc;
	ampool_internal_t* child;

	pd->pool = &pool->ops;
	pd->pool_name = pool->name;
	pd->parent = (parent ? &parent->ops : NULL);
	pd->parent_name = (parent ? parent->name : NULL);
	pd->size = pool->size;
	pd->elements = pool->element_count;

	rc = callback(pd, user_data);
	if (rc != AMRC_SUCCESS)
		return AMRC_ERROR;

	amlist_for_each_entry(child, &pool->children_list, sibling_link) {
		rc = _ampool_diag(child, pool, pd, callback, user_data);
		if (rc != AMRC_SUCCESS)
			return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

/* This will iterate over all allocated pools and call the provided callback with the proper stats for each
 * WARNING: While this is running, no other task may add or delete pools
 * USE WITH CAUTION */
void ampool_diag(ampool_diag_cb_t callback, void* user_data)
{
	ampool_diag_t pd;
	ampool_internal_t* pool;
	amrc_t rc;

	if (callback == NULL)
		return;

	pthread_mutex_lock(&globals.hierarchy_mutex);
	amlist_for_each_entry(pool, &globals.root_pools, sibling_link) {
		rc = _ampool_diag(pool, NULL, &pd, callback, user_data);
		if (rc != AMRC_SUCCESS)
			break;
	}
	pthread_mutex_unlock(&globals.hierarchy_mutex);
}


static amrc_t _ampool_elem_diag(ampool_bucket_t* bucket, ampool_elem_diag_cb_t callback, ampool_elem_diag_t* ed, void* user_data)
{
	ampool_chunk_t* chunk;
	amrc_t rc = AMRC_SUCCESS;

	pthread_mutex_lock(&bucket->mutex);

	amlist_for_each_entry(chunk, &bucket->used_list, link) {
		ed->elem = chunk->data;
		ed->elem_name = chunk->name;
		ed->elem_size = chunk->size;

		rc = callback(ed, user_data);
		if (rc != AMRC_SUCCESS) {
			break;
		}
	}

	pthread_mutex_unlock(&bucket->mutex);

	return rc;
}

/* This will iterate over all allocated elements in the pool and call the provided callback with the proper stats for each
 * pool_stats will be populated at the end of the run.
 * Note that if there were concurrent allocations/deletions while this was run, the coherent stats will be  pool_stats and not ampool_get_size().
 *
 * WARNING: While this is running, some allocation sizes may block. Others may go through.
 * USE WITH CAUTION */
void ampool_elem_diag(ampool_t* ops, ampool_elem_diag_cb_t callback, ampool_diag_stats_t* pool_stats, void* user_data)
{
	ampool_internal_t* pool = container_of(ops, ampool_internal_t, ops);
	uint32_t i;
	ampool_elem_diag_t ed;
	amrc_t rc;

	if (pool_stats)
		memset(pool_stats, 0, sizeof(*pool_stats));

	if (pool == NULL || callback == NULL)
		return;

	ed.pool = &pool->ops;
	ed.pool_name = pool->name;

	for (i = 0; i < AMPOOL_STEP_COUNT; i++) {
		rc = _ampool_elem_diag(&pool->steps[i], callback, &ed, user_data);
		if (rc != AMRC_SUCCESS)
			return;
	}
	_ampool_elem_diag(&pool->oversized, callback, &ed, user_data);
}
