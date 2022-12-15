#include <stdio.h>

#include "test_base.h"

#include "libam/libam_stats.h"

#include "libam/libam_log.h"
#include "libam/libam_replace.h"
#include "libam/libam_time.h"

#ifdef NDEBUG
#include <stdio.h>
#undef assert
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); fflush(stderr); abort(); }} while(0)
#else
#include <assert.h>
#endif

#ifdef err
#undef err
#endif
#ifdef log
#undef log
#endif
#define err(fmt, args...) amlog_sink_log(AMLOG_ERROR, 0, fmt, ##args)
#define log(fmt, args...) amlog_sink_log(AMLOG_DEBUG, 0, fmt, ##args)

#ifndef uint128_t
#include <stdint.h>
typedef __uint128_t uint128_t;
#endif

static amrc_t test_amstat_upd_min()
{
	uint32_t errors = 0;
	uint64_t i;
	amstat_range_t stat;
	_Static_assert(sizeof(stat.min) == 8);

	/* Init */
	amstat_init(&stat);
	if (stat.min != UINT64_MAX) {
		err("Failed to validate initial value\n");
		errors++;
	}

	/* Sanity */
	for (i = UINT64_MAX;; i /= 2) {
		amstat_upd(&stat, i);
		if (stat.min != i) {
			err("Failed to validate updating value %lu\n", i);
			errors++;
		}
		if (i < UINT64_MAX) {
			amstat_upd(&stat, i + 1);
			if (stat.min != i) {
				err("Failed to validate updating value %lu\n", i + 1);
				errors++;
			}
		}
		if (i == 0)
			break;
	}

	/* Overflow */
	amstat_init(&stat);
	amstat_upd(&stat, 23);
	amstat_upd(&stat, UINT64_MAX);
	amstat_upd(&stat, UINT64_MAX);
	amstat_upd(&stat, UINT64_MAX);
	if (stat.min != 23) {
		err("Failed to validate overflow value\n");
		errors++;
	}

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amstat_upd_max()
{
	uint32_t errors = 0;
	uint64_t i;
	amstat_range_t stat;
	_Static_assert(sizeof(stat.max) == 8);

	/* Init */
	amstat_init(&stat);
	if (stat.max != 0) {
		err("Failed to validate initial value\n");
		errors++;
	}
	amstat_upd(&stat, 0);
	if (stat.max != 0) {
		err("Failed to validate initial update value\n");
		errors++;
	}

	/* Sanity */
	for (i = 1;; i *= 2) {
		amstat_upd(&stat, i);
		if (stat.max != i) {
			err("Failed to validate updating value %lu\n", i);
			errors++;
		}
		amstat_upd(&stat, i - 1);
		if (stat.max != i) {
			err("Failed to validate updating value %lu\n", i - 1);
			errors++;
		}
		if (i > UINT64_MAX / 2)
			break;
	}

	/* Overflow */
	amstat_init(&stat);
	amstat_upd(&stat, UINT64_MAX - 6);
	amstat_upd(&stat, UINT64_MAX / 2);
	amstat_upd(&stat, UINT64_MAX / 2);
	amstat_upd(&stat, UINT64_MAX / 2);
	amstat_upd(&stat, UINT64_MAX / 2);
	amstat_upd(&stat, UINT64_MAX / 2);
	amstat_upd(&stat, UINT64_MAX / 2);
	if (stat.max != UINT64_MAX - 6) {
		err("Failed to validate overflow value\n");
		errors++;
	}

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amstat_upd_num()
{
	uint32_t errors = 0;
	uint64_t add;
	uint64_t expected;
	uint128_t tracker = 0;
	amstat_range_t stat;
	amstat_range_t copy;

	/* Init */
	amstat_init(&stat);
	if (stat.num != 0) {
		err("Failed to validate initial value\n");
		errors++;
	}

	/* single update */
	amstat_upd(&stat, 0);
	if (stat.num != 1) {
		err("Failed to validate first update\n");
		errors++;
	}

	memcpy(&copy, &stat, sizeof(stat));
	while (stat.num != UINT64_MAX) {
		add = amtime_now() & 0xFFFFF;
		add |= 0x0000FFF000000000LU;
		stat.num += add;
		if (copy.num > stat.num)
			stat.num = UINT64_MAX - 1;
		expected = stat.num + 1LU;
		memcpy(&copy, &stat, sizeof(stat));
		amstat_upd(&stat, 0);
		if (stat.num != expected) {
			err("Failed to validate count from %lu to %lu\n", copy.num, expected);
			errors++;
		}
		copy.num = expected;
		if (memcmp(&stat, &copy, sizeof(stat)) != 0) {
			err("Unexpected change of stucture from %lu to %lu\n", expected - 1, expected);
			errors++;
		}
	}

	/* Overflow check */
	tracker = stat.num;
	tracker++;
	amstat_upd(&stat, 0);
	if ((stat.num != (UINT64_MAX / 2) + 1) || tracker != ((uint128_t)UINT64_MAX) + 1LLU) {
		err("Failed to validate overflow\n");
		errors++;
	}

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amstat_upd_sum()
{
	uint32_t errors = 0;
	uint128_t tracker = 0;
	uint64_t copy;
	uint64_t val = 1;
	amstat_range_t stat;
	uint64_t vals[] = { 0, 1, 2, 5, 10, UINT64_MAX / 4, UINT64_MAX / 2, UINT64_MAX / 1.5, UINT64_MAX - 5, UINT64_MAX - 2, UINT64_MAX - 1, UINT64_MAX };
	uint64_t starting_point;
	uint64_t test;

	_Static_assert(sizeof(tracker) == 16);

	/* Init */
	amstat_init(&stat);
	if (stat.sum != 0) {
		err("Failed to validate initial value\n");
		errors++;
	}

	/* First value */
	amstat_upd(&stat, 0);
	if (stat.sum != 0) {
		err("Failed to validate zero value\n");
		errors++;
	}

	/* Sanity */
	while (stat.sum != UINT64_MAX) {
		copy = stat.sum;
		tracker = stat.sum;
		tracker += (uint128_t)val;
		if (tracker > ((uint128_t)UINT64_MAX)) {
			val = UINT64_MAX - stat.sum;
			tracker = UINT64_MAX;
		}
		stat.ssq = 0; /* Disable ssq-triggered overflow! */
		amstat_upd(&stat, val);
		if (stat.sum != tracker) {
			err("Failed to validate sum going from %lu + %lu, received %lu\n", copy, val, stat.sum);
			errors++;
		}

		val = (val << 1) | 0x1LU;
	}

	/* Sometimes overflowing */
	for (starting_point = 0; starting_point < ARRAY_SIZE(vals); starting_point++) {
		for (test = 0; test < ARRAY_SIZE(vals); test++) {
			amstat_init(&stat);
			stat.num = 1;
			stat.sum = vals[starting_point];
			tracker = stat.sum;
			tracker += (uint128_t)vals[test];
			if (tracker <= UINT64_MAX)
				val = tracker;
			else { /* Overflow, figure out expected value */
				val = (tracker / 2LLU) & 0xFFFFFFFFFFFFFFFFLU;
			}

			amstat_upd(&stat, vals[test]);
			if (stat.sum != val) {
				err("Failed to validate sum of %lu and %lu, expected %lu, received %lu\n", vals[starting_point], vals[test], val, stat.sum);
				errors++;
			}
		}
	}

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amstat_upd_ss()
{
	uint32_t errors = 0;
	amstat_range_t stat;

	uint64_t vals[] = { 0, 1, 2, 5, 10, 0xFFFF, 0xFFFFFFFE, 0x100000000LU, 0xFFFFFFFF, 0x100000001LU, UINT64_MAX - 1, UINT64_MAX };
	uint128_t squares[ARRAY_SIZE(vals)];
	uint128_t current;
	uint64_t i;
	uint64_t overflow_counter;
	uint64_t expected;

	/* Initialize arrays */
	for (i = 0; i < ARRAY_SIZE(vals); i++) {
		squares[i] = ((uint128_t)vals[i]) * ((uint128_t)vals[i]);
	}

	/* Init */
	amstat_init(&stat);
	if (stat.ssq != 0) {
		err("Failed to validate initial value\n");
		errors++;
	}

	/* Check capping at UINT64_MAX, validate stays there */
	/* Order of VALS is important. 0xFFFFFFFE & 0xFFFFFFFF will trigger ssq-overflow, and we're not down for that here */
	current = 0;
	for (i = 0; i < ARRAY_SIZE(vals) * 2; i++) {
		current += squares[i % ARRAY_SIZE(vals)];
		expected = current;
		if (squares[i % ARRAY_SIZE(vals)] > UINT64_MAX || current > UINT64_MAX) {
			current = UINT64_MAX;
			expected = UINT64_MAX;
		}
		amstat_upd(&stat, vals[i % ARRAY_SIZE(vals)]);
		if (stat.ssq != expected) {
			err("Failed to validate sum of squares at %lu, value %lu, expected %lu, received %lu\n", i, vals[i % ARRAY_SIZE(vals)], expected, stat.ssq);
			errors++;
		}
	}

	/* validate ssq overflow */
	amstat_init(&stat);
	overflow_counter = 0;
	while (overflow_counter < 16) {
		for (i = 0; i < ARRAY_SIZE(vals); i++) {
			if (vals[i] > AMSTAT_MAX_SSQ_VAL)
				continue;
			current = stat.ssq;
			current += squares[i];
			if (current > UINT64_MAX) {
				overflow_counter++;
				expected = current / 2LLU;
			}
			else
				expected = current;

			amstat_upd(&stat, vals[i]);
			if (stat.ssq != expected) {
				err("Failed to validate overflow sum of squares at %lu, value %lu, expected %lu, received %lu\n", i, vals[i], expected, stat.ssq);
				errors++;
			}
		}
	}

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

#define error_on_diff(ref, test) error_on_diff_((ref), (test), __LINE__)
static int error_on_diff_(const amstat_range_t* ref, const amstat_range_t* tst, int line)
{
	if (memcmp(ref, tst, sizeof(amstat_range_t)) != 0) {
		err("Comparison failed on line %d: Expected - min %lu, avg %lu, max %lu, num %lu, sum %lu, ssq %lu\n", line, ref->min, ref->avg, ref->max, ref->num, ref->sum, ref->ssq);
		err("Comparison failed on line %d: Received - min %lu, avg %lu, max %lu, num %lu, sum %lu, ssq %lu\n", line, tst->min, tst->avg, tst->max, tst->num, tst->sum, tst->ssq);
		return 1;
	}
	return 0;
}

static amrc_t test_amstat_upd_test_special_cases()
{
	uint32_t errors = 0;
	amstat_range_t stat;
	amstat_range_t expt;

	amstat_init(&stat);
	amstat_upd(&stat, UINT64_MAX);
	memcpy(&expt, &stat, sizeof(stat));
	amstat_upd(&stat, UINT64_MAX);
	errors += error_on_diff(&expt, &stat);

	amstat_init(&stat);
	amstat_upd(&stat, 0);
	stat.num = UINT64_MAX;
	memcpy(&expt, &stat, sizeof(stat));
	expt.num = (((uint128_t)UINT64_MAX) + 1LLU) / 2LLU;
	amstat_upd(&stat, 0);
	errors += error_on_diff(&expt, &stat);

	amstat_init(&stat);
	amstat_upd(&stat, 0);
	stat.num = UINT64_MAX;
	memset(&expt, 0, sizeof(expt));
	expt.min = 0;
	expt.max = UINT64_MAX;
	expt.num = 9223372036854775808LU;
	expt.sum = UINT64_MAX / 2;
	expt.avg = expt.sum / expt.num;
	expt.ssq = UINT64_MAX;
	amstat_upd(&stat, UINT64_MAX);
	errors += error_on_diff(&expt, &stat);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

int main()
{
	amrc_t rc;
	test_t tests[] = {
			TEST(test_amstat_upd_min),
			TEST(test_amstat_upd_max),
			TEST(test_amstat_upd_num),
			TEST(test_amstat_upd_sum),
			TEST(test_amstat_upd_ss),
			TEST(test_amstat_upd_test_special_cases),
	};
	test_set_t set = {
			.name = "stats_tests",
			.count = ARRAY_SIZE(tests),
			.tests = tests
	};

	amlog_sink_init(AMLOG_FLAGS_ABORT_ON_ERROR);
	rc = run_tests(&set);
	amlog_sink_term();

	return (rc == AMRC_SUCCESS ? 0 : -1);
}
