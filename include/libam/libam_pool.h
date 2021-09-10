#ifndef _LIBAM_MEMPOOL_H_
#define _LIBAM_MEMPOOL_H_

#include "libam/libam_types.h"
#include "libam/libam_stats.h"

/* THIS IS A MUTEXED memory allocation pool.
 * Any data larger than AMPOOL_MAX_STEPPED bytes is allocated and freed as it is
 * Anything lower than the cutoff is re-used.
 */

typedef enum ampool_flags {
	AMPOOL_VALIDATE_ON_FREE	= 1 << 0, /* Run a simple validation of memory before freeing a chunk */
} ampool_flags_t;

typedef struct ampool_bucket_stats {
	uint64_t		element_size;

	uint64_t		used_size; // In bytes, only of used elements. Excludes overhead.
	amstat_range_t	used_size_range;
	uint64_t		total_size; // In bytes, of used + free elements. Excludes overhead.
	amstat_range_t	total_size_range;

	uint64_t		used_element_count; // Only of used elements. Excludes overhead.
	amstat_range_t	used_element_count_range;
	uint64_t		total_element_count; // Of used + free elements. Excludes overhead.
	amstat_range_t	total_element_count_range;
} ampool_bucket_stats_t;

struct ampool;

typedef void*	(*ampool_alloc_t)(struct ampool* pool, uint32_t size, const char* name);
typedef void*	(*ampool_realloc_t)(struct ampool* pool, void * ptr, uint32_t old_size, uint32_t new_size, const char* name);
typedef void	(*ampool_free_t)(struct ampool* pool, void * ptr, uint32_t size);
typedef uint64_t (*ampool_get_size_t)(struct ampool* pool);
typedef void	(*ampool_pool_free_t)(struct ampool* pool);

typedef struct ampool {
	ampool_alloc_t alloc;
	ampool_realloc_t realloc;
	ampool_free_t free;
	ampool_get_size_t get_size; /* Size is only for the current pool, lineage doesn't count. */
	ampool_pool_free_t pool_free; /* Frees this pool, and all children */
} ampool_t;

void ampool_init();
void ampool_term();

ampool_t* ampool_pool_alloc_flags_named(ampool_t* parent, ampool_flags_t flags, const char* name);
#define ampool_pool_alloc_flags(parent, flags) ampool_pool_alloc_flags_named((parent), (flags), __LOCATION__)
#define ampool_pool_alloc(parent) ampool_pool_alloc_flags((parent), (parent) != NULL ? (parent)->flags : 0)

#define ampool_pool_free(pool)		((pool)->pool_free((pool)))

#define ampool_get_size(pool)		((pool)->get_size((pool)))

#define ampool_alloc(pool, size)	((pool)->alloc((pool), (size), __LOCATION__))
#define ampool_realloc(pool, ptr, old_size, new_size) ((pool)->realloc((pool), (ptr), (old_size), (new_size), __LOCATION__))
#define ampool_free(pool, ptr)		((pool)->free((pool), (ptr)))


/*
 * Disagnostic tools:
 * -----------------------------------------------
 * These are intended for validation and debugging
 */

typedef struct ampool_diag {
	ampool_t* pool;
	const char* pool_name;

	ampool_t* parent; /* If NULL, it means a root pool */
	const char* parent_name;

	uint64_t size; /* Total allocated bytes, excluding overhead and free blocks */
	uint64_t elements; /* Total number of allocated elemens in the pool, does not include children */
} ampool_diag_t;

/* Return AMRC_SUCCESS to keep looping */
typedef amrc_t (*ampool_diag_cb_t)(const ampool_diag_t* diag_info, void* user_data);


/* This will iterate over all allocated pools and call the provided callback with the proper stats for each
 * WARNING: While this is running, no other task may add or delete pools
 * USE WITH CAUTION */
void ampool_diag(ampool_diag_cb_t callback, void* user_data);


typedef struct ampool_elem_diag {
	ampool_t* pool;
	const char* pool_name;

	const void* elem;
	const char* elem_name;
	uint32_t elem_size;
} ampool_elem_diag_t;

typedef struct ampool_diag_stats {
	/* Statistics of all elements in pool, across all sizes */
} ampool_diag_stats_t;

/* Return AMRC_SUCCESS to keep looping */
typedef amrc_t (*ampool_elem_diag_cb_t)(const ampool_elem_diag_t* elem_diag_info, void* user_data);

/* This will iterate over all allocated elements in the pool and call the provided callback with the proper stats for each
 * pool_stats will be populated at the end of the run.
 * Note that if there were concurrent allocations/deletions while this was run, the coherent stats will be  pool_stats and not ampool_get_size().
 *
 * WARNING: While this is running, some allocation sizes may block. Others may go through.
 * USE WITH CAUTION */
void ampool_elem_diag(ampool_t* pool, ampool_elem_diag_cb_t callback, ampool_diag_stats_t* pool_stats, void* user_data);

#endif /* _LIBAM_MEMPOOL_H_ */
