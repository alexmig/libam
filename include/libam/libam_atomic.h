#ifndef _LIBAM_ATOMIC_H_
#define _LIBAM_ATOMIC_H_

// These return the value of *ptr before the change
#define amsync_add(ptr, val)	__sync_fetch_and_add((ptr), (val))
#define amsync_sub(ptr, val)	__sync_fetch_and_sub((ptr), (val))
#define amsync_or(ptr, val)	__sync_fetch_and_or((ptr), (val))
#define amsync_and(ptr, val)	__sync_fetch_and_and((ptr), (val))
#define amsync_xor(ptr, val)	__sync_fetch_and_xor((ptr), (val))
#define amsync_nand(ptr, val)	__sync_fetch_and_nand((ptr), (val))
#define amsync_inc(ptr)	amsync_add((ptr), 1)
#define amsync_dec(ptr)	amsync_sub((ptr), 1)

/**
 * This is an atomic operation for:
 * if (*ptr != oldval) return 0;
 * *ptr = newval;
 * return 1;
 */
#define amsync_swap(ptr, oldval, newval) __sync_bool_compare_and_swap((ptr), (oldval), (newval))

/**
 * Issue a full memory barrier
 */
#define amsync() __sync_synchronize();


/**
 * Note that there are other atomics, but we don't use them often
 */

#endif
