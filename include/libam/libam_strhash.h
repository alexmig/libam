#ifndef _LIBAM_STRHASH_H_
#define _LIBAM_STRHASH_H_

#include <stdint.h>

#include <libam/libam_types.h>

/* TO MAKE THIS THREAD SAFE USE LIBAM_STRHASH_FLAG_USE_LOCK */

typedef enum strhash_flags {
	LIBAM_STRHASH_FLAG_NONE 		= 0 << 0,
	LIBAM_STRHASH_FLAG_FIXED_SIZE 	= 1 << 0, /* Do not resize once allocated */
	LIBAM_STRHASH_FLAG_USE_LOCK 	= 1 << 1, /* Lock access operations, make insert/delete/find thread safe */
	LIBAM_STRHASH_FLAG_DUP_KEYS		= 1 << 2, /* Allocate memory internally and copy keys when inserting */
	LIBAM_STRHASH_FLAG_OVERWRITE	= 1 << 3, /* If key inserted exists, free it before iserting new one */
	LIBAM_STRHASH_FLAG_NO_FREE_CB	= 1 << 4, /* When issuing amstrhash_term, do not trigger free callbacks */
} strhash_flags_t;

enum strhash_defaults {
	LIBAM_STRHASH_DEFAULT_INITIAL_CAPACITY = 8,
	LIBAM_STRHASH_DEFAULT_RESIZE_PERCENT = 65, /* Double size once 65% of current capacity reached */
	LIBAM_STRHASH_DEFAULT_RESIZE_PER_BUCKET = 4, /* Double size once this many elements are in a single bucket */
	LIBAM_STRHASH_DEFAULT_FREE_SIZE = 4, /* Only keep this many deteled spares */
};

struct amstrhash;
typedef struct amstrhash amstrhash_t;

struct amstrhash_entry;
typedef struct amstrhash_entry amstrhash_entry_t;

typedef void (*amstrhash_delete_callback_t)(const char* key, void* value);

typedef struct amstrhash_attr {
	uint64_t percent_threashold; /* See LIBAM_STRHASH_DEFAULT_RESIZE_PERCENT default. 1+. 0 For default. */
	uint64_t bucket_threashold;	/* See LIBAM_STRHASH_DEFAULT_RESIZE_PER_BUCKET default. 2+. 0 For default. */
	uint64_t free_size; /* See LIBAM_STRHASH_DEFAULT_FREE_SIZE default. */
	amstrhash_delete_callback_t on_delete; /* Can be null for no callbacks (Default behavior) */
} amstrhash_attr_t;

/* Create a string hash
 * inital_capacity - Leave 0 for defaults.
 * attr - (Optional) Specific configuration attributes. If NULL, defaults everything.
 * @Returns strhash handle / NULL on error */
amstrhash_t* amstrhash_init(uint64_t inital_capacity, strhash_flags_t flags, const amstrhash_attr_t* attr);
void amstrhash_term(amstrhash_t* strhash);

/* Inserts an element into a hash table.
 * NOTE: Will perform resizes unless LIBAM_STRHASH_FLAG_FIXED_SIZE is specified.
 * NOTE: Will invoke deletion callbacks in calling thread.
 * key - Null-terminated string
 * value - NULL is acceptable
 * old_key - Optional. If key already present and no OVERWRITE specified, argument set to existing key.
 * @Returns AMRC_SUCCESS on successful insertion / AMRC_ERROR on errors or when key exists */
amrc_t amstrhash_insert(amstrhash_t* strhash, const char* key, void* value, amstrhash_entry_t** old_key);

/* Locate key in table.
 * key - Null-terminated string
 * @Returns pointer to existing key / NULL if not found or on error */
amstrhash_entry_t* amstrhash_find(amstrhash_t* strhash, const char* key);

/* Remove an already-located key from table.
 * NOTE: Will invoke deletion callbacks in calling thread.
 * @Returns AMRC_SUCCESS / AMRC_ERROR
 */
amrc_t amstrhash_remove(amstrhash_t* strhash, amstrhash_entry_t*);

/* Same as amstrhash_remove, only it issues amstrhash_find first */
amrc_t amstrhash_remove_key(amstrhash_t* strhash, const char* key);

/* @Returns current capacity. 0 on error */
uint64_t amstrhash_get_capacity(const amstrhash_t* strhash);

/* @Returns current size. 0 on error */
uint64_t amstrhash_get_size(const amstrhash_t* strhash);


/* Basic access methods for entry */
const char* amstrhash_get_ent_key(amstrhash_entry_t* ent);
void* amstrhash_get_ent_value(amstrhash_entry_t* ent);
void amstrhash_set_ent_value(amstrhash_entry_t* ent, void* value);

#endif /* _LIBAM_STRHASH_H_ */
