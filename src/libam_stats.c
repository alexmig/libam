#include <stdio.h>

#include "libam/libam_stats.h"

/**
 * Returns a pointer to a null-terminated string containing
 * formatted statistic (no newline)
 */
void amstat_2str(const amstat_range_t* stat, char* buff, uint64_t buf_len)
{
	snprintf(buff, buf_len, "%15lu\t%15lu\t%15lu\t(%lu)",
			(stat->min == UINT64_MAX ? 0 : stat->min), stat->avg,
			stat->max, stat->num);
	buff[buf_len - 1] = '\0';
}
