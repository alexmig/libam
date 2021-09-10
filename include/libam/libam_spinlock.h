#ifndef _LIBAM_SPINLOCK_H_
#define _LIBAM_SPINLOCK_H_

#include "libam_types.h"
#include "libam_atomic.h"

typedef volatile uint64_t amspinlock_t;

#define AMSPINLOCK_UNLOCKED (0UL)

// Blocking! Will spin while waiting for lock
#define amspinlock_lock(lck, id)	while (!amsync_swap(lck, AMSPINLOCK_UNLOCKED, id))

// Returns true if unlock was successfull
// DO NOT CALL WITHOUT AQUIRING LOCK FIRST!
#define amspinlock_unlock(lck, id)	amsync_swap(lck, id, AMSPINLOCK_UNLOCKED)

#endif
