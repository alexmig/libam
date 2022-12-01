#ifndef _LIBAM_LOGSINK_H_
#define _LIBAM_LOGSINK_H_

#include <stdint.h>

#include <libam/libam_types.h>
#include <libam/libam_time.h>
#include <libam/libam_cqueue.h>
#include <libam/libam_replace.h>

typedef enum amlog_level { /* Suggested log levels */
	AMLOG_DEBUG = 10,
	AMLOG_INFO = 6,
	AMLOG_WARNING = 3,
	AMLOG_ERROR = 1,
	AMLOG_CRITICAL = 0,
} amlog_level_t;

typedef enum amlog_flags {
	AMLOG_FLAGS_NONE = 0,
	AMLOG_FLAGS_USE_THREAD		= 1 << 0, /* This will offload all direct callbacks to thread(s).
	 	 	 	 	 	 	 This also means that logs will be formatted on the spot, rather than on-demand */
	AMLOG_FLAGS_AVOID_SOURCE_LINES	= 1 << 1, /* Do not propagate source code location */
	AMLOG_FLAGS_BLOCK_ON_ERROR	= 1 << 2, /* Instead of failing, block until able to succeed */
	AMLOG_FLAGS_ABORT_ON_ERROR	= 1 << 3, /* Instead of failing, abort */

} amlog_flags_t;

typedef struct amlog_line {
	amtime_t	timestamp;	/* Timestamp at time of logging of line */
	uint64_t	level;		/* Numerical log level of line */
	uint64_t	mask;		/* Mask of components for log line. Can be 0 */
	const char*	file;		/* Name of file in which log line originated */
	int		line;		/* Line number in which log line originated */
	const char	message[256];	/* Null terminated message to print. May contain newlines. */
	int		message_length;	/* Length in bytes of message passed, excluding null terminator */
} amlog_line_t;

struct amlog_sink;
typedef struct amlog_sink amlog_sink_t;

typedef void (*amlog_sink_cb_t)(amlog_sink_t* sink, void* user_data, const amlog_line_t* line);


/* Register a direct sink.
 * A amlog_line_t structure will be passed to callback directly when messages arrive.
 * NOTE: This can happen in parallel to other callbacks.
 *
 * @Returns Pinter to sink handle / NULL on error
 */
amlog_sink_t* amlog_sink_register_direct(const char* name, amlog_sink_cb_t callback, void* user_data);

/* Register a queued sink.
 * Free amlog_line_t structures are expected to be in out_queue.
 * Upon receiving a log line amlog_line_t will be dequeued from out_queue, populated, and placed into in_queue.
 * It is the responsibility of the sink itself to maintain and populate out_queue.
 *
 * @Returns Pinter to sink handle / NULL on error
 */
amlog_sink_t* amlog_sink_register_queued(const char* name, amcqueue_t* in_queue, amcqueue_t* out_queue, void* user_data);


/* Set log level of sink.
 * A message will only be routed to sink if line.level <= sink.level.
 *
 * Return previous log level / 0 if sink is NULL
 */
uint64_t amlog_sink_set_level(amlog_sink_t* sink, uint64_t new_level);

/* Set log mask of sink.
 * A message will only be routed to sink if either line.mask == 0, or line.mask & sink.mask != 0
 *
 * Return previous mask / 0 if sink is NULL
 */
uint64_t amlog_sink_set_mask(amlog_sink_t* sink, uint64_t new_mask);

/* Unregisters the sink and invalidates the handle */
void amlog_sink_unregister(amlog_sink_t* sink);


/* Main logging function -
 * level - Only sinks that are configured to print this level or above will see the message. Set to 0 to always send to all sinks.
 * mask - Only sinks that have at least one component mask in common with this line will see this message. Set to 0 to always send to all sinks.
 * @Returns AMRC_SUCCESS if printed successfully / AMRC_ERROR if failed to format, allocate memory, or queue */
amrc_t amlog_sink_message(const char* file, int line, uint64_t level, uint64_t mask, const char *fmt, ...);
#define amlog_sink_log(level, mask, fmt, args...)	amlog_sink_message(__FILE_NAME__, __LINE__, level, mask, fmt, ##args)


/* Initialized sink fnctionality.
 * NOT THREAD SAFE with other init/term */
amrc_t amlog_sink_init(amlog_flags_t flags);

/* Terminate sink fnctionality.
 * NOT THREAD SAFE with other init/term */
void amlog_sink_term();


/* A handful of useful formatting functions */

/* Convert a buffer to binary string.
 * @Returns length used (Excluding null terminator) */
int amlog_hex(const void* buf, int length, char* output, int output_length);

/* Convert buffer of binary data into dump format
 * start_offset being x will result in the first line address to be aligned to x. Use 0 if unsure.
 * @Returns length used (Excluding null terminator) */
int amlog_dump(const void* buf, int length, char* output, int output_length, uint64_t start_offset);


/* Default supplied sink */

/* A default log sink that simply outputs lines to STDOUT is supplied here */
void amlog_sink_dafault_stdout(amlog_sink_t* sink , void* user_data, const amlog_line_t* line);

/* An easy first setup, that still enables performance, would look like the following:
 * rc = amlog_sink_init(AMLOG_FLAGS_USE_THREAD);
 * assert(rc == AMRC_SUCCESS);
 * rc = amlog_sink_register_direct("Default", amlog_sink_dafault_stdout, NULL);
 * assert(rc == AMRC_SUCCESS);
 */

#endif /* _LIBAM_LOGSINK_H_ */
