#ifndef _LIBAM_STATS_H_
#define _LIBAM_STATS_H_

#include <stdint.h>

typedef struct amstat_range {
	uint64_t	max;
	uint64_t	min;
	uint64_t	sum;
	uint64_t	ss;
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
	stat->ss = 0;
}

/**
 * Adds a value to the metric
 */
static inline void amstat_upd(amstat_range_t* stat, uint64_t val)
{
	uint64_t square = val * val;

	if (stat->max < val) stat->max = val;
	if (stat->min > val) stat->min = val;

	if (stat->ss == UINT64_MAX || val > UINT32_MAX || stat->ss + square < stat->ss)
		stat->ss = UINT64_MAX;
	else
		stat->ss += square;

	if (stat->sum + val < stat->sum) {
		stat->num /= 2;
		stat->sum /= 2;
		stat->ss /= 2;
	}

	stat->sum += val;
	stat->num++;
	stat->avg = stat->sum / stat->num;
}

/**
 * Adds the contents of one stat to the other
 */
static inline void amstat_add(amstat_range_t* to, amstat_range_t* from)
{
	if (to->max < from->max) to->max = from->max;
	if (to->min > from->min) to->min = from->min;

	// Overflow protection

	if (to->ss == UINT64_MAX || from->ss == UINT64_MAX) {
		to->ss = UINT64_MAX;
	}
	else if (to->ss + from->ss < to->ss) {
		to->num /= 2;
		to->sum /= 2;
		to->ss /= 2;
		from->num /= 2;
		from->sum /= 2;
		from->ss /= 2;
	}

	to->sum += from->sum;
	to->num += from->num;
	to->avg = (to->num == 0 ? 0 : to->sum / to->num);
}

/**
 * Returns a pointer to a null-terminated string containing
 * formatted statistic (no newline)
 */
void amstat_2str(const amstat_range_t* stat, char* buff, uint64_t buf_len);


#endif
