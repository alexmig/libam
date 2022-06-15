#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include "libam/libam_log.h"

#include "libam/libam_list.h"


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

/* TODO: Locks? */
static amlist_t direct_sinks = { .next = &direct_sinks, .prev = &direct_sinks};
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

	amlist_add(&direct_sinks, &sink->link);

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

	amlist_add(&queued_sinks, &sink->link);

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
	if (sink != NULL) {
		amlist_del(&sink->link);
		free(sink);
	}
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

	ent.level = level;
	ent.mask = mask;
	ent.file = file;
	ent.function = function;
	ent.line = line;

	/* TODO: readlock? lockless? */

	/* First, push copies of messages to queued handlers */
	amlist_for_each_entry(sink, &queued_sinks, link) {
		if (sink->mask != 0 && ent.mask != 0 && (sink->mask & ent.mask) == 0)
			continue;
		if (sink->level < ent.level)
			continue;
		if (!formatted) {
			ent.timestamp = amtime_now();
			va_start(ap, fmt);
			ent.message_length = vasprintf((char**)&ent.message, fmt, ap);
			va_end(ap);
			if (ent.message_length < 0)
				return AMRC_ERROR;
			formatted = 1;
		}

		rc = amcqueue_deq(sink->out_queue, (void**)&queued_ent);
		if (rc != AMRC_SUCCESS)
			continue;

		memcpy(queued_ent, &ent, sizeof(ent));
		rc = amcqueue_enq(sink->in_queue, queued_ent);
		if (rc != AMRC_SUCCESS)
			continue;
	}

	/* TODO: Threads? */
	amlist_for_each_entry(sink, &direct_sinks, link) {
		if (sink->mask != 0 && ent.mask != 0 && (sink->mask & ent.mask) == 0)
			continue;
		if (sink->level < ent.level)
			continue;
		if (!formatted) {
			ent.timestamp = amtime_now();
			va_start(ap, fmt);
			ent.message_length = vasprintf((char**)&ent.message, fmt, ap);
			va_end(ap);
			if (ent.message_length < 0)
				return AMRC_ERROR;
			formatted = 1;
		}

		memcpy(&ent_direct, &ent, sizeof(ent));
		sink->callback(sink, sink->user_data, &ent_direct);
	}

	/* TODO: Locks? lockless? */

	if (ent.message != NULL)
		free((char*)ent.message);

	return AMRC_SUCCESS;
}

/* Initialized sink fnctionality */
amrc_t amlog_sink_init()
{
	return AMRC_SUCCESS;
}

/* Terminate sink fnctionality */
void amlog_sink_term()
{
}

/* Convert a buffer to binary string */
void amlog_hex(const void* buf, int length, char* output, int output_length)
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
}

/* Convert buffer of binary data into dump format
 * start_offset being x will result in the first line address to be aligned to x. Use 0 if unsure. */
void amlog_dump(const void* buf, int length, char* output, int output_length, uint64_t start_offset)
{
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
	}

	*output = '\0';
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
