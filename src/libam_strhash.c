#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "libam/libam_strhash.h"

#include "libam/libam_types.h"
#include "libam/libam_list.h"
#include "libam/libam_hash.h"

struct amstrhash_entry {
	amlink_t	link;
	const char*	key;
	uint64_t	key_hash;
	void*		value;
};

typedef struct amstrhash_bucket {
	amlist_t			entries;
	uint64_t			size;
} amstrhash_bucket_t;

struct amstrhash {
	strhash_flags_t		flags;
	amstrhash_attr_t	attr;
	amlist_t			free_ents;
	uint64_t			free_size;
	uint64_t			capacity;
	uint64_t			size;
	pthread_rwlock_t	lock;
	amstrhash_bucket_t*	buckets;
};

static inline amstrhash_bucket_t* amstrhash_bucket(amstrhash_t* hash, uint64_t hash_value)
{
	return &hash->buckets[hash_value % hash->capacity];
}

static void amstrhash_init_buckets(amstrhash_t* hash)
{
	uint64_t i;

	for (i = 0; i < hash->capacity; i++) {
		amlist_init(&hash->buckets[i].entries);
		hash->buckets[i].size = 0;
	}
}

/* Create a string hash
 * inital_capacity - Leave 0 for defaults.
 * attr - (Optional) Specific configuration attributes. If NULL, defaults everything.
 * @Returns strhash handle / NULL on error */
amstrhash_t* amstrhash_init(uint64_t inital_capacity, strhash_flags_t flags, const amstrhash_attr_t* attr)
{

	amstrhash_t* hash;
	int rc;

	hash = malloc(sizeof(*hash));
	if (hash == NULL) {
		return NULL;
	}
	memset(hash, 0, sizeof(*hash));
	hash->flags = flags;

	if (inital_capacity < LIBAM_STRHASH_DEFAULT_INITIAL_CAPACITY)
		inital_capacity = LIBAM_STRHASH_DEFAULT_INITIAL_CAPACITY;

	if (attr != NULL)
		memcpy(&hash->attr, attr, sizeof(hash->attr));
	if (hash->attr.percent_threashold <= 0)
		hash->attr.percent_threashold = LIBAM_STRHASH_DEFAULT_RESIZE_PERCENT;
	if (hash->attr.bucket_threashold <= 1)
		hash->attr.bucket_threashold = LIBAM_STRHASH_DEFAULT_RESIZE_PER_BUCKET;

	rc = pthread_rwlock_init(&hash->lock, NULL);
	if (rc != 0) {
		free(hash);
		return NULL;
	}

	hash->buckets = malloc(inital_capacity * sizeof(*hash->buckets));
	if (hash->buckets == NULL) {
		free(hash);
		return NULL;
	}
	hash->capacity = inital_capacity;
	hash->size = 0;
	amstrhash_init_buckets(hash);
	amlist_init(&hash->free_ents);

	return hash;
}

/* Should already be locked */
static amrc_t amstrhash_ent_new(amstrhash_t* hash, amstrhash_bucket_t* bucket, const char* key, uint64_t hash_value, void* value)
{
	amstrhash_entry_t* ent;

	if ((hash->flags & LIBAM_STRHASH_FLAG_DUP_KEYS) && key != NULL) {
		key = strdup(key);
		if (key == NULL) {
			return AMRC_ERROR;
		}
	}

	/* Obtain ent */
	if (!amlist_empty(&hash->free_ents)) {
		assert(hash->free_size > 0);
		ent = amlist_first_entry(&hash->free_ents, amstrhash_entry_t, link);
		amlist_del(&ent->link);
		hash->free_size--;
	}
	else {
		assert(hash->free_size == 0);
		ent = malloc(sizeof(*ent));
		if (ent == NULL) {
			if (hash->flags & LIBAM_STRHASH_FLAG_DUP_KEYS)
				free((char*)key);
			return AMRC_ERROR;
		}
	}

	ent->key = key;
	ent->key_hash = hash_value;
	ent->value = value;
	amlist_add(&bucket->entries, &ent->link);

	bucket->size++;
	hash->size++;

	return AMRC_SUCCESS;
}

static void amstrhash_ent_remove(amstrhash_t* hash, amstrhash_entry_t* ent, ambool_t use_cb, ambool_t use_lock)
{
	amstrhash_bucket_t* bucket;

	if (use_lock && (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK))
		pthread_rwlock_wrlock(&hash->lock);

	/* Issue callback */
	if (hash->attr.on_delete && use_cb) {
		hash->attr.on_delete(ent->key, ent->value);
	}
	ent->value = NULL;

	/* Remove key */
	if (hash->flags & LIBAM_STRHASH_FLAG_DUP_KEYS)
		free((char*)ent->key);
	ent->key = NULL;

	/* Remove ent */
	amlist_del(&ent->link);
	hash->size--;

	bucket = amstrhash_bucket(hash, ent->key_hash);
	bucket->size--;

	if (hash->free_size >= hash->attr.free_size)
		free(ent);
	else {
		amlist_add(&hash->free_ents, &ent->link);
		hash->free_size++;
	}

	if (use_lock && (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK))
		pthread_rwlock_unlock(&hash->lock);
}

/* Should already be locked */
static amrc_t amstrhash_upsize(amstrhash_t* hash)
{
	uint64_t i;
	uint64_t old_cap = hash->capacity;
	amstrhash_bucket_t* old_bkts = hash->buckets;

	amstrhash_bucket_t*	cur_bkt;
	amstrhash_bucket_t*	new_bkt;
	amstrhash_entry_t* ent;

	hash->capacity *= 2;
	hash->buckets = malloc(hash->capacity * sizeof(*hash->buckets));
	if (hash->buckets == NULL) {
		hash->capacity = old_cap;
		hash->buckets = old_bkts;
		return AMRC_ERROR;
	}
	amstrhash_init_buckets(hash);

	for (i = 0; i < old_cap; i++) {
		cur_bkt = &old_bkts[i];
		while (!amlist_empty(&cur_bkt->entries)) {
			assert(cur_bkt->size > 0);

			ent = amlist_first_entry(&cur_bkt->entries, amstrhash_entry_t, link);

			amlist_del(&ent->link);
			cur_bkt->size--;

			new_bkt = amstrhash_bucket(hash, ent->key_hash);
			amlist_add(&new_bkt->entries, &ent->link);
			new_bkt->size++;
		}
		assert(cur_bkt->size == 0);
	}

	free(old_bkts);
	return AMRC_SUCCESS;
}

void amstrhash_term(amstrhash_t* hash)
{
	uint64_t i;
	amlist_t* list;
	amstrhash_entry_t* ent;

	if (hash== NULL)
		return;

	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_wrlock(&hash->lock);

	for (i = 0; i < hash->capacity; i++) {
		list = &hash->buckets[i].entries;
		while (!amlist_empty(list)) {
			ent = amlist_first_entry(list, amstrhash_entry_t, link);

			amstrhash_ent_remove(hash, ent, !(hash->flags & LIBAM_STRHASH_FLAG_NO_FREE_CB), am_false);
		}
	}

	while (!amlist_empty(&hash->free_ents)) {
		ent = amlist_first_entry(&hash->free_ents, amstrhash_entry_t, link);
		amlist_del(&ent->link);
		hash->free_size--;
		free(ent);
	}

	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_unlock(&hash->lock);
}

static uint64_t calc_hash(const char* key, uint64_t* out_len)
{
	if (key == NULL) {
		if (out_len != NULL)
			*out_len = 0;
		return UINT64_MAX;
	}
	return amshash(key, out_len);
}

/* Inserts an element into a hash table.
 * NOTE: Will perform resizes unless LIBAM_STRHASH_FLAG_FIXED_SIZE is specified.
 * NOTE: Will invoke deletion callbacks in calling thread.
 * key - Null-terminated string
 * value - NULL is acceptable
 * old_key - Optional. If key already present and no OVERWRITE specified, argument set to existing key.
 * @Returns AMRC_SUCCESS on successful insertion / AMRC_ERROR on errors or when key exists */
amrc_t amstrhash_insert(amstrhash_t* hash, const char* key, void* value, amstrhash_entry_t** old_key)
{
	uint64_t hash_value;
	amstrhash_bucket_t*	bucket;
	amlist_t* list;
	amstrhash_entry_t* ent;
	amstrhash_entry_t* found = NULL;
	amrc_t ret = AMRC_ERROR;
	ambool_t should_resize;

	hash_value = calc_hash(key, NULL);

	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_wrlock(&hash->lock);

	bucket = amstrhash_bucket(hash, hash_value);
	list = &bucket->entries;

	amlist_for_each_entry(ent, list, link) {
		if (ent->key_hash != hash_value)
			continue;
		if (ent->key != key) {
			found = ent;
			break;
		}
		if (strcmp(ent->key, key) == 0){
			found = ent;
			break;
		}
	}

	if (found != NULL) {
		/* OVERWRITE specified, just swap out the value */
		if (hash->flags & LIBAM_STRHASH_FLAG_OVERWRITE) {
			if (hash->attr.on_delete)
				hash->attr.on_delete(key, ent->value);
			ent->value = value;
			ret = AMRC_SUCCESS;
			goto done;
		}

		/* OVERWRITE not specified, fail and set old_key */
		if (old_key != NULL) {
			*old_key = found;
		}
		goto done;
	}

	/* Entry not found in bucket. Need to add new entry */

	/* Is a resize needed? */
	if (!(hash->flags & LIBAM_STRHASH_FLAG_FIXED_SIZE)) {
		should_resize = (hash->size + 1 >= ((hash->attr.percent_threashold * hash->capacity) / 100));
		should_resize = (should_resize || bucket->size + 1 >= (hash->attr.bucket_threashold));
		if (should_resize) {
			ret = amstrhash_upsize(hash);
			if (ret != AMRC_SUCCESS)
				goto done;
			bucket = amstrhash_bucket(hash, hash_value);
		}
	}

	/* Sizing is as desired, add new entry */
	ret = amstrhash_ent_new(hash, bucket, key, hash_value, value);

done:
	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_unlock(&hash->lock);

	return ret;

}

/* Same as amstrhash_find, only exposing use_lock */
static amstrhash_entry_t* amstrhash_find_lock(amstrhash_t* hash, const char* key, ambool_t use_lock)
{
	uint64_t hash_value;
	amstrhash_bucket_t* bucket;
	amlist_t* list;
	amstrhash_entry_t* ent;
	amstrhash_entry_t* out = NULL;

	hash_value = calc_hash(key, NULL);

	if (use_lock && (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK))
		pthread_rwlock_rdlock(&hash->lock);

	bucket = amstrhash_bucket(hash, hash_value);
	list = &bucket->entries;
	amlist_for_each_entry(ent, list, link) {
		if (ent->key_hash != hash_value)
			continue;
		if (ent->key != key) {
			out = ent;
			break;
		}
		if (strcmp(ent->key, key) == 0){
			out = ent;
			break;
		}
	}

	if (use_lock && (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK))
		pthread_rwlock_unlock(&hash->lock);

	return out;
}

/* Locate key in table.
 * key - Null-terminated string
 * @Returns pointer to existing key / NULL if not found or on error */
amstrhash_entry_t* amstrhash_find(amstrhash_t* hash, const char* key)
{
	return amstrhash_find_lock(hash, key, am_true);
}


/* Remove an already-located key from table.
 * NOTE: Will invoke deletion callbacks in calling thread.
 * @Returns AMRC_SUCCESS / AMRC_ERROR
 */
amrc_t amstrhash_remove(amstrhash_t* hash, amstrhash_entry_t* ent)
{
	if (ent == NULL)
		return AMRC_ERROR;
	amstrhash_ent_remove(hash, ent, am_true, am_true);
	return AMRC_SUCCESS;
}

/* Same as amstrhash_remove, only it issues amstrhash_find first */
amrc_t amstrhash_remove_key(amstrhash_t* hash, const char* key)
{
	amstrhash_entry_t* ent;

	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_wrlock(&hash->lock);

	ent = amstrhash_find_lock(hash, key, am_false);
	if (ent == NULL)
		return AMRC_ERROR;

	amstrhash_ent_remove(hash, ent, am_true, am_false);

	if (hash->flags & LIBAM_STRHASH_FLAG_USE_LOCK)
		pthread_rwlock_unlock(&hash->lock);
	return AMRC_SUCCESS;
}

/* @Returns current capacity. 0 on error */
uint64_t amstrhash_get_capacity(const amstrhash_t* hash)
{
	return hash->capacity;
}

/* @Returns current size. 0 on error */
uint64_t amstrhash_get_size(const amstrhash_t* hash)
{
	return hash->size;
}


const char* amstrhash_get_ent_key(amstrhash_entry_t* ent)
{
	return ent->key;
}

void* amstrhash_get_ent_value(amstrhash_entry_t* ent)
{
	return ent->value;
}

void amstrhash_set_ent_value(amstrhash_entry_t* ent, void* value)
{
	ent->value = value;
}
