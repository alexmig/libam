#ifndef _LIBAM_STATS_H_
#define _LIBAM_STATS_H_

/* NOTICE: THESE FUNCTION ARE NOT THREAD SAFE! */

#include <stdint.h>
#include <assert.h>

#define AMSTAT_MAX_SSQ_VAL (0xFFFFFFFF)

typedef struct amstat_range {
	uint64_t	max;
	uint64_t	min;
	uint64_t	sum;
	uint64_t	ssq;
	uint64_t	avg;
	uint64_t	num;
} amstat_range_t;

/**
 * Sets workable initial values for a specified metric
 */
static inline void amstat_init(amstat_range_t* stat)
{
	stat->min = UINT64_MAX;
	stat->max = 0;
	stat->sum = 0;
	stat->num = 0;
	stat->avg = 0;
	stat->ssq = 0; /* Sum of squares. If too large of a value is passed or accumulated, set to UINT64_MAX */
}

/**
 * Adds a value to the metric
 * THIS FUNCTION IS NOTH THREAD SAFE
 */
static inline void amstat_upd(amstat_range_t* stat, uint64_t val)
{
	__uint128_t u128;
	uint64_t square = 0;

	if (stat->max < val) stat->max = val;
	if (stat->min > val) stat->min = val;

	if (stat->ssq == UINT64_MAX || val > AMSTAT_MAX_SSQ_VAL) /* Cannot possibly calculate sum of suqres */
		stat->ssq = UINT64_MAX;
	else
		square = val * val;

	if (stat->num == UINT64_MAX || stat->sum + val < stat->sum || stat->ssq + square < stat->ssq) {
		_Static_assert(sizeof(u128) == 16);

		u128 = stat->sum;
		u128 += (__uint128_t)val;
		stat->sum = u128 / 2LLU;

		if (stat->ssq != UINT64_MAX) {
			u128 = stat->ssq;
			u128 += (__uint128_t)square;
			stat->ssq = u128 / 2LLU;
		}

		if (stat->num == UINT64_MAX)
			stat->num = (UINT64_MAX / 2) + 1;
		else {
			stat->num++;
			stat->num /= 2;
		}
	}
	else {
		stat->sum += val;
		stat->ssq += square;
		stat->num++;
	}
	stat->avg = stat->sum / stat->num;
}

/**
 * Adds the contents of one stat to the other
 * THIS FUNCTION IS NOTH THREAD SAFE
 */
static inline void amstat_add(amstat_range_t* to, amstat_range_t* from)
{
	if (to->max < from->max) to->max = from->max;
	if (to->min > from->min) to->min = from->min;

	// Overflow protection

	if (to->ssq == UINT64_MAX || from->ssq == UINT64_MAX) {
		to->ssq = UINT64_MAX;
	}
	else if (to->ssq + from->ssq < to->ssq) {
		to->num /= 2;
		to->sum /= 2;
		to->ssq /= 2;
		from->num /= 2;
		from->sum /= 2;
		from->ssq /= 2;
	}

	to->ssq += from->ssq;
	to->sum += from->sum;
	to->num += from->num;
	to->avg = (to->num == 0 ? 0 : to->sum / to->num);
}

/**
 * Returns a pointer to a null-terminated string containing
 * formatted statistic (no newline)
 * THIS FUNCTION IS NOTH THREAD SAFE
 */
void amstat_2str(const amstat_range_t* stat, char* buff, uint64_t buf_len);


#endif
