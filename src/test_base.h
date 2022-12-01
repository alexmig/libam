#ifndef _LIBAM_TEST_BASE_H_
#define _LIBAM_TEST_BASE_H_

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "libam/libam_types.h"
#include "libam/libam_list.h"
#include "libam/libam_log.h"

#define log(fmt, args...)  do { fprintf(stdout, fmt, ##args); fflush(stdout); } while (0)
#define err(fmt, args...)  do { fprintf(stdout, "ERROR: "fmt, ##args); fflush(stdout); } while (0)

#ifdef NDEBUG
#include <stdio.h>
#undef assert
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); fflush(stderr); abort(); }} while(0)
#else
#include <assert.h>
#endif

typedef amrc_t (*test_cb_t)();

typedef struct test {
	const char* name;
	test_cb_t	func;
} test_t;

typedef struct test_set {
	const char* name;
	uint32_t count;
	const test_t* tests;
} test_set_t;

typedef struct logline {
	amlink_t link;
	char buffer[256];
} logline_t;

enum _test_constants {
	_LOQ_QUEUE_SIZE = 2048,
};
typedef struct log_queue {
	amcqueue_t* in_queue;
	amcqueue_t* out_queue;
	amlog_sink_t* sink;
	amlist_t list; /* logline_t */
	pthread_t pt;
	ambool_t keep_running;
	volatile uint64_t started;
	volatile uint64_t sleeps;
	amlog_line_t lines[_LOQ_QUEUE_SIZE];
} log_queue_t;

#define TEST(x) { .func = x, .name = #x }

static int sleep_microseconds(uint64_t us)
{
	struct timespec sleep_time = { .tv_sec = us / 1000000, .tv_nsec = us % 1000000 };
	return nanosleep(&sleep_time, NULL);
}

static void signal_handler(UNUSED int signo)
{
}

static void set_signal_handler()
{
	int rc;
	struct sigaction sa = {
		    .sa_handler = signal_handler,
		    .sa_flags = SA_SIGINFO,
	};
	sigemptyset(&sa.sa_mask);
	rc = sigaction(SIGUSR1, &sa, NULL);
	assert(rc == 0);
}

static void* _test_logger_thread(void* arg)
{
	log_queue_t* lq = arg;
	amlog_line_t* ent;
	amrc_t rc;
	char ts_buffer[26];
	time_t time;
	struct tm *tm;
	uint64_t usec;
	const char* level;
	logline_t* line;

	set_signal_handler();
	lq->started = 1;
	while (lq->keep_running) {
		rc = amcqueue_deq(lq->in_queue, (void**)&ent);
		if (rc != AMRC_SUCCESS) {
			amsync_inc(&lq->sleeps);
			sleep_microseconds(100);
			continue;
		}

		/* time */
		usec = ent->timestamp % AMTIME_SEC;
		time = ent->timestamp / AMTIME_SEC;
		tm = localtime(&time);
		strftime(ts_buffer, 26, "%Y-%m-%d %H:%M:%S", tm);

		/* level */
		     if (ent->level <= AMLOG_CRITICAL)	level = "CRITICAL";
		else if (ent->level <= AMLOG_ERROR)		level = "ERROR   ";
		else if (ent->level <= AMLOG_WARNING)	level = "WARNING ";
		else if (ent->level <= AMLOG_INFO)		level = "INFO    ";
		else if (ent->level <= AMLOG_DEBUG)		level = "DEBUG   ";
		else									level = "OTHER   ";

		line = malloc(sizeof(*line));
		if (line == NULL) {
			err("Failed to allocate memory for a log line\n");
			abort();
		}
		else {
			snprintf(line->buffer, sizeof(line->buffer), "%s.%06lu %s:%d %s %.*s", ts_buffer, usec, ent->file, ent->line, level, ent->message_length, ent->message);
			line->buffer[sizeof(line->buffer) - 1] = '\0';
			amlist_add_tail(&lq->list, &line->link);
		}

		rc = amcqueue_enq(lq->out_queue, ent);
		if (rc != AMRC_SUCCESS) {
			err("Failed to enqueue log line\n");
			abort();
		}
	}

	return NULL;
}

static log_queue_t* _test_start_logger()
{
	log_queue_t* lq = NULL;
	int i;
	amrc_t rc;

	lq = malloc(sizeof(*lq));
	if (lq == NULL) {
		err("Failed to allocate logger memory\n");
		goto error;
	}
	memset(lq, 0, sizeof(*lq));
	lq->keep_running = am_true;
	amlist_init(&lq->list);

	lq->in_queue = amcqueue_alloc(_LOQ_QUEUE_SIZE);
	if (lq->in_queue == NULL) {
		err("Failed to allocate logger in queue\n");
		goto error;
	}

	lq->out_queue = amcqueue_alloc(_LOQ_QUEUE_SIZE);
	if (lq->out_queue == NULL) {
		err("Failed to allocate logger out queue\n");
		goto error;
	}

	for (i = 0; i < _LOQ_QUEUE_SIZE; i++) {
		rc = amcqueue_enq(lq->out_queue, &lq->lines[i]);
		if (rc != AMRC_SUCCESS) {
			err("Failed to queue %d lines\n", i);
			goto error;
		}
	}

	rc = pthread_create(&lq->pt, NULL, _test_logger_thread, lq);
	if (rc != 0) {
		err("Failed to start logger thread\n");
		goto error;
	}

	while (lq->started == 0)
		sleep_microseconds(1000);

	return lq;

error:
	if (lq != NULL) {
		if (lq->in_queue != NULL)
			amcqueue_free(lq->in_queue);
		if (lq->out_queue != NULL)
			amcqueue_free(lq->out_queue);
		free(lq);
	}
	return NULL;
}

static void _test_flush_logger(log_queue_t* lq, ambool_t print, ambool_t force_register)
{
	ambool_t restore_sink;
	logline_t* line;
	uint64_t sleeps;

	if (lq == NULL) {
		err("_test_flush_logger used on NULL pointer\n");
		return;
	}

	restore_sink = (lq->sink != NULL);
	amlog_sink_unregister(lq->sink);
	lq->sink = NULL;

	/* Wake logger thread up */
	sleeps = lq->sleeps;
	pthread_kill(lq->pt, SIGUSR1);
	while (sleeps == lq->sleeps)
		sleep_microseconds(50);

	/* Flush loglines out */
	while (!amlist_empty(&lq->list)) {
		line = amlist_first_entry(&lq->list, logline_t, link);
		amlist_del(&line->link);
		if (print)
			fprintf(stdout, line->buffer);
		free(line);
	}
	fflush(stdout);

	if (force_register || restore_sink) {
		lq->sink = amlog_sink_register_queued("test_log_sink", lq->in_queue, lq->out_queue, NULL);
		assert(lq->sink != NULL);
		amlog_sink_set_mask(lq->sink, UINT64_MAX);
	}
}

static void _test_stop_logger(log_queue_t* lq)
{
	int rc;

	if (lq == NULL) {
		err("_test_stop_logger used on NULL pointer\n");
		return;
	}

	amlog_sink_unregister(lq->sink);
	lq->sink = NULL;
	_test_flush_logger(lq, am_false, am_false);

	lq->keep_running = am_false;
	rc = pthread_join(lq->pt, NULL);
	if (rc != 0) {
		err("Failed to stop logger thread\n");
		abort();
	}

	if (lq->in_queue != NULL)
		amcqueue_free(lq->in_queue);
	if (lq->out_queue != NULL)
		amcqueue_free(lq->out_queue);

	free(lq);
}

/* Execute a test set, timing functionalities. */
static amrc_t run_tests(const test_set_t* set)
{
	uint32_t i;
	amrc_t rc;
	amrc_t ret = AMRC_SUCCESS;
	amtime_t start = amtime_now();
	double elapsed_seconds;
	log_queue_t* lq;

	log("Starting test set %s\n", set->name);
	lq = _test_start_logger();
	assert(lq != NULL);
	_test_flush_logger(lq, am_false, am_true);

	for (i = 0; i < set->count; i++) {
		log("\t%s: %s: ", set->name, set->tests[i].name);
		rc = set->tests[i].func();
		if (rc != AMRC_SUCCESS) {
			log("ERROR\n");
			ret = AMRC_ERROR;
			_test_flush_logger(lq, am_true, am_false);
		}
		else {
			log("OK\n");
			_test_flush_logger(lq, am_false, am_false);
		}
	}

	elapsed_seconds = ((double)amtime_now() - start) / ((double)AMTIME_SEC);
	_test_stop_logger(lq);

	if (ret == AMRC_SUCCESS)
		log("Done test set %s done successfully (%.2lf seconds)!\n", set->name, elapsed_seconds);
	else
		err("Done test set %s encountered errors (%.2lf seconds)!\n", set->name, elapsed_seconds);
	return ret;
}

/* Example usage:

#include "libam/libam_log.h"
#include "test_base.h"

amrc_t test_function1();
amrc_t test_function2();

int main()
{
	amrc_t rc;
	test_t tests[] = {
			TEST(test_function1),
			TEST(test_function2),
	};
	test_set_t set = {
			.name = "example_test_set",
			.count = ARRAY_SIZE(tests),
			.tests = tests
	};

	amlog_sink_init(AMLOG_FLAGS_ABORT_ON_ERROR);
	rc = run_tests(&set);
	amlog_sink_term();
	return (rc == AMRC_SUCCESS ? 0 : -1);
}

 */

#endif /* _LIBAM_TEST_BASE_H_ */
