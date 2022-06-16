#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include "libam/libam_log.h"

#include "libam/libam_list.h"


enum log_constatns {
	THREAD_LINE_BUFFER_SIZE = 2048,
	THREAD_POLL_FREQ = 5 * AMTIME_MSEC,
	BLOCK_POLL_FREQ = 2 * AMTIME_MSEC,
};

struct amlog_sink {
	amlink_t	link;
	const char*	name;
	void*		user_data;
	uint64_t	level;
	uint64_t	mask;
	amlog_sink_cb_t	callback;
	amcqueue_t* in_queue;
	amcqueue_t* out_queue;
};

typedef struct log_thread {
	pthread_t		thread;
	ambool_t		keep_running;
	amcqueue_t* 	in_queue;
	amcqueue_t* 	out_queue;
	amlog_line_t	line_buffer[THREAD_LINE_BUFFER_SIZE];
	amlog_sink_t*	sink;
} log_thread_t;

static ambool_t abort_on_error = am_false;
static ambool_t block_on_error = am_false;
static log_thread_t* log_thread = NULL;

static pthread_rwlock_t direct_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static amlist_t direct_sinks = { .next = &direct_sinks, .prev = &direct_sinks};
static pthread_rwlock_t queued_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static amlist_t queued_sinks = { .next = &queued_sinks, .prev = &queued_sinks};

/* Register a direct sink.
 * A amlog_line_t structure will be passed to callback directly when messages arrive.
 * NOTE: This can happen in parallel to other callbacks.
 *
 * @Returns Pinter to sink handle / NULL on error
 */
amlog_sink_t* amlog_sink_register_direct(const char* name, amlog_sink_cb_t callback, void* user_data)
{
	amlog_sink_t* sink;

	if (callback == NULL)
		return NULL;

	sink = malloc(sizeof(*sink));
	if (sink == NULL)
		return NULL;
	memset(sink, 0, sizeof(*sink));

	sink->name = name;
	sink->level = UINT64_MAX;
	sink->user_data = user_data;
	sink->callback = callback;

	pthread_rwlock_wrlock(&direct_rwlock);
	amlist_add(&direct_sinks, &sink->link);
	pthread_rwlock_unlock(&direct_rwlock);

	return sink;
}

/* Register a queued sink.
 * Free amlog_line_t structures are expected to be in out_queue.
 * Upon receiving a log line amlog_line_t will be dequeued from out_queue, populated, and placed into in_queue.
 * It is the responsibility of the sink itself to maintain and populate out_queue.
 *
 * @Returns Pinter to sink handle / NULL on error
 */
amlog_sink_t* amlog_sink_register_queued(const char* name, amcqueue_t* in_queue, amcqueue_t* out_queue, void* user_data)
{
	amlog_sink_t* sink;

	if (in_queue == NULL || out_queue == NULL)
		return NULL;

	sink = malloc(sizeof(*sink));
	if (sink == NULL)
		return NULL;
	memset(sink, 0, sizeof(*sink));

	sink->name = name;
	sink->level = UINT64_MAX;
	sink->user_data = user_data;
	sink->in_queue = in_queue;
	sink->out_queue = out_queue;

	pthread_rwlock_wrlock(&queued_rwlock);
	amlist_add(&queued_sinks, &sink->link);
	pthread_rwlock_unlock(&queued_rwlock);

	return sink;
}


/* Set log level of sink.
 * A message will only be routed to sink if line.level <= sink.level.
 *
 * Return previous log level / 0 if sink is NULL
 */
uint64_t amlog_sink_set_level(amlog_sink_t* sink, uint64_t new_level)
{
	uint64_t old;

	if (sink == NULL)
		return 0;

	old = sink->level;
	sink->level = new_level;
	return old;
}

/* Set log mask of sink.
 * A message will only be routed to sink if either line.mask == 0, or line.mask & sink.mask != 0
 *
 * Return previous mask / 0 if sink is NULL
 */
uint64_t amlog_sink_set_mask(amlog_sink_t* sink, uint64_t new_mask)
{
	uint64_t old;

	if (sink == NULL)
		return 0;

	old = sink->mask;
	sink->mask = new_mask;
	return old;
}

/* Unregisters the sink and invalidates the handle */
void amlog_sink_unregister(amlog_sink_t* sink)
{
	static pthread_rwlock_t* lock;

	if (sink != NULL) {
		lock = (sink->callback == NULL ? &queued_rwlock : &direct_rwlock);
		pthread_rwlock_wrlock(lock);
		amlist_del(&sink->link);
		pthread_rwlock_unlock(lock);
		free(sink);
	}
}

amrc_t amlog_sink_dequeue(amlog_sink_t* sink, amlog_line_t** ent)
{
	static struct timespec block_delay = { .tv_sec = 0, .tv_nsec = BLOCK_POLL_FREQ};
	amrc_t rc;

	while (1) {
		rc = amcqueue_deq(sink->out_queue, (void**)ent);
		if (rc == AMRC_SUCCESS)
			return rc;
		if (abort_on_error)
			abort();
		if (block_on_error) {
			nanosleep(&block_delay, NULL);
			continue;
		}

		break;
	}

	return AMRC_ERROR;
}

amrc_t amlog_sink_enqueue(amlog_sink_t* sink, amlog_line_t* ent)
{
	static struct timespec block_delay = { .tv_sec = 0, .tv_nsec = BLOCK_POLL_FREQ};
	amrc_t rc;

	while (1) {
		rc = amcqueue_enq(sink->in_queue, ent);
		if (rc == AMRC_SUCCESS)
			return rc;
		if (abort_on_error)
			abort();
		if (block_on_error) {
			nanosleep(&block_delay, NULL);
			continue;
		}

		break;
	}

	return AMRC_ERROR;
}

amrc_t amlog_sink_message(const char* file, const char* function, int line, uint64_t level, uint64_t mask, const char *fmt, ...)
{
	int formatted = 0;
	amlog_line_t ent;
	amlog_line_t ent_direct;
	amlog_sink_t* sink;
	amlog_line_t* queued_ent;
	amrc_t rc;
	va_list ap;
	char* non_const_message = (char*)ent.message;

	ent.level = level;
	ent.mask = mask;
	ent.file = file;
	ent.function = function;
	ent.line = line;

	/* First, push copies of messages to queued handlers */
	pthread_rwlock_rdlock(&queued_rwlock); /* Until we have a lockless iterable structure... */
	amlist_for_each_entry(sink, &queued_sinks, link) {
		if (sink->mask != 0 && ent.mask != 0 && (sink->mask & ent.mask) == 0)
			continue;
		if (sink->level < ent.level)
			continue;

		/* Need to send it out. Make sure it's formatted */
		if (!formatted) {
			ent.timestamp = amtime_now();
			va_start(ap, fmt);
			ent.message_length = vsnprintf(non_const_message, sizeof(ent.message) - 1, fmt, ap);
			va_end(ap);
			non_const_message[sizeof(ent.message) - 1] = '\0';
			if (ent.message_length < 0) {
				pthread_rwlock_unlock(&queued_rwlock);
				return AMRC_ERROR;
			}
			formatted = 1;
		}

		rc = amlog_sink_dequeue(sink, &queued_ent);
		if (rc != AMRC_SUCCESS)
			continue;

		memcpy(queued_ent, &ent, sizeof(ent));
		rc = amlog_sink_enqueue(sink, queued_ent);
		if (rc != AMRC_SUCCESS)
			continue;
	}
	pthread_rwlock_unlock(&queued_rwlock);

	if (log_thread == NULL) {
		pthread_rwlock_rdlock(&direct_rwlock); /* Until we have a lockless iterable structure... */
		amlist_for_each_entry(sink, &direct_sinks, link) {
			if (sink->mask != 0 && ent.mask != 0 && (sink->mask & ent.mask) == 0)
				continue;
			if (sink->level < ent.level)
				continue;
			if (!formatted) {
				ent.timestamp = amtime_now();
				va_start(ap, fmt);
				ent.message_length = vsnprintf(non_const_message, sizeof(ent.message) - 1, fmt, ap);
				va_end(ap);
				non_const_message[sizeof(ent.message) - 1] = '\0';
				if (ent.message_length < 0) {
					pthread_rwlock_unlock(&direct_rwlock);
					return AMRC_ERROR;
				}
				formatted = 1;
			}

			memcpy(&ent_direct, &ent, sizeof(ent));
			sink->callback(sink, sink->user_data, &ent_direct);
		}
		pthread_rwlock_unlock(&direct_rwlock);
	}

	return AMRC_SUCCESS;
}

static void* amlog_direct_callback_thread_func(void* data)
{
	amrc_t rc;
	log_thread_t* log_thread = data;
	amlog_line_t* ent;
	amlog_line_t ent_copy;
	amlog_sink_t* sink;
	struct timespec delay = { .tv_sec = 0, .tv_nsec = THREAD_POLL_FREQ};

	while (am_true) {
		rc = amcqueue_deq(log_thread->in_queue, (void**)&ent);
		if (rc != AMRC_SUCCESS) {
			if (!log_thread->keep_running)
				break;

			nanosleep(&delay, NULL);
			continue;
		}

		pthread_rwlock_rdlock(&direct_rwlock); /* Until we have a lockless iterable structure... */
		amlist_for_each_entry(sink, &direct_sinks, link) {
			if (sink->mask != 0 && ent->mask != 0 && (sink->mask & ent->mask) == 0) {
				continue;
			}
			if (sink->level < ent->level) {
				continue;
			}
			memcpy(&ent_copy, ent, sizeof(*ent));
			sink->callback(sink, sink->user_data, &ent_copy);
		}
		pthread_rwlock_unlock(&direct_rwlock);

		rc = amcqueue_enq(log_thread->out_queue, ent);
		assert(rc == AMRC_SUCCESS);
	}

	return NULL;
}

/* Initialized sink fnctionality.
 * NOT THREAD SAFE with other init/term */
amrc_t amlog_sink_init(amlog_flags_t flags)
{
	int i;
	amrc_t rc;

	if ((flags & AMLOG_FLAGS_BLOCK_ON_ERROR) && (flags & AMLOG_FLAGS_BLOCK_ON_ERROR))
		return AMRC_ERROR;
	abort_on_error = !!(flags & AMLOG_FLAGS_ABORT_ON_ERROR);
	block_on_error = !!(flags & AMLOG_FLAGS_BLOCK_ON_ERROR);

	if (flags & AMLOG_FLAGS_USE_THREAD) {
		assert(log_thread == NULL);

		log_thread = malloc(sizeof(*log_thread));
		if (log_thread == NULL)
			goto error;
		memset(log_thread, 0, sizeof(*log_thread));
		log_thread->keep_running = am_true;

		/* Allocate queues for sink */
		log_thread->in_queue = amcqueue_alloc(THREAD_LINE_BUFFER_SIZE);
		if (log_thread->in_queue == NULL)
			goto error;

		log_thread->out_queue = amcqueue_alloc(THREAD_LINE_BUFFER_SIZE);
		if (log_thread->out_queue == NULL)
			goto error;

		for (i = 0; i < THREAD_LINE_BUFFER_SIZE; i++) {
			rc = amcqueue_enq(log_thread->out_queue, &log_thread->line_buffer[i]);
			if (rc != AMRC_SUCCESS)
				goto error;
		}

		log_thread->sink = amlog_sink_register_queued("amlog direct callback thread", log_thread->in_queue, log_thread->out_queue, NULL);
		if (log_thread->sink == NULL)
			goto error;

		rc = pthread_create(&log_thread->thread, NULL, amlog_direct_callback_thread_func, log_thread);
		if (rc != 0) {
			goto error;
		}
	}

	return AMRC_SUCCESS;

error:
	if (log_thread != NULL) {
		if (log_thread->sink != NULL)
			amlog_sink_unregister(log_thread->sink);
		if (log_thread->in_queue != NULL)
			amcqueue_free(log_thread->in_queue);
		if (log_thread->out_queue != NULL)
			amcqueue_free(log_thread->out_queue);
		free(log_thread);
		log_thread = NULL;
	}

	return AMRC_ERROR;
}

/* Terminate sink fnctionality.
 * NOT THREAD SAFE */
void amlog_sink_term()
{
	if (log_thread != NULL) {
		log_thread->keep_running = am_false;
		pthread_join(log_thread->thread, NULL);
		if (log_thread->sink != NULL)
			amlog_sink_unregister(log_thread->sink);
		if (log_thread->in_queue != NULL)
			amcqueue_free(log_thread->in_queue);
		if (log_thread->out_queue != NULL)
			amcqueue_free(log_thread->out_queue);
		free(log_thread);
		log_thread = NULL;
	}
}


/* Convert a buffer to binary string */
int amlog_hex(const void* buf, int length, char* output, int output_length)
{
	uint8_t* p = (uint8_t*)buf;

	while (length > 0 && output_length > 2) {
		snprintf(output, 3, "%02x", *p);
		p++;
		length--;
		output += 2;
		output_length -= 2;
	}
	*output = '\0';
	return p - (uint8_t*)buf;
}

/* Convert buffer of binary data into dump format
 * start_offset being x will result in the first line address to be aligned to x. Use 0 if unsure. */
int amlog_dump(const void* buf, int length, char* org_output, int output_length, uint64_t start_offset)
{
	char* output = org_output;
	char hex[16*3 + 2], asc[16 + 2];
	const uint8_t* ptr = buf;
	const uint8_t* end = ptr + length;
	uint8_t skip = (start_offset & 0xF);
	uint64_t line_address = start_offset - skip;
	uint64_t i, hi, ai;
	int added;

	for (; line_address < start_offset + length; line_address += 16) {
		hi = 0;
		ai = 0;
		for (i = 0; i < 16; i++) {
			if (i == 8) {
				hex[hi++] = ' ';
				asc[ai++] = ' ';
			}

			if (skip > 0 || ptr >= end) {
				sprintf(&hex[hi], "   "); hi += 3;
				asc[ai++] = ' ';
				skip--;
			}
			else {
				asc[ai++] = isprint(*ptr) ? *ptr : '.';
				sprintf(&hex[hi], "%02x ", *ptr); hi += 3;
				ptr++;
			}
		}
		hex[hi] = '\0';
		asc[ai] = '\0';
		added = snprintf(output, output_length, "%06lx %s %s\n", line_address, hex, asc);
		if (added >= output_length) {
			output += output_length - 1;
			output_length = 1;
			break;
		}

		output += added;
		output_length -= added;
	}

	*output = '\0';

	return output - org_output;
}


/* A default log sink that simply outputs lines to STDOUT is supplied */
void amlog_sink_dafault_stdout(UNUSED amlog_sink_t* sink, UNUSED void* user_data, const amlog_line_t* line)
{
	char buffer[26];
	time_t time;
	struct tm *tm;
	uint64_t usec;

	usec = line->timestamp % AMTIME_SEC;
	time = line->timestamp / AMTIME_SEC;
	tm = localtime(&time);
	strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm);

	fprintf(stdout, "%s.%06lu %lu %lx %.*s", buffer, usec, line->level, line->mask, line->message_length, line->message);
}
