#ifndef _LIBAM_LOG_H_
#define _LIBAM_LOG_H_

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

typedef struct amlog {
	uint64_t level; // Log level of line must be below this value to be printed.
	uint64_t component_mask; // To be priinted, line must have at least one bit in common with the mask.
	int fd;
} amlog_t;

extern const amlog_t* dlog;

#define AMLOG_PLAIN(lg, fmt, args...)	dprintf((lg)->fd, fmt, ##args)
#ifdef DEBUG
#define AMLOG_PREFIX(lg, fmt, args...)	AMLOG_PLAIN(lg, "%s:%05d "fmt, __FUNCTION__, __LINE__, ##args)
#else
#define AMLOG_PREFIX(lg, fmt, args...)	AMLOG_PLAIN(lg, fmt, ##args)
#endif
#define AMLOG_DEFUALT(fmt, args...)		AMLOG_PREFIX(dlog, fmt, ##args);

#define amlog_printable(lg, lvl, cmp)	(((lg)->level >= (lvl)) && (((lg)->component_mask == 0) || ((lg)->component_mask & (cmp))))
#define amlog(lg, lvl, cmp, fmt, args...) { if (amlog_printable((lg), (lvl), (cmp))) { AMLOG_PREFIX((lg), fmt, ##args); }}
#define amlog_flush(lg) fdatasync((lg)->fd)

void amlog_init(amlog_t* log, char* filename);
void amlog_term(amlog_t* log);

// Printouts

void amlog_binary(amlog_t* log, const char* msg, const void* buf, const uint64_t length);
#define amlog_binary_mask(lg, lvl, cmp, msg, buf, len) { if (amlog_printable((lg), (lvl), (cmp))) { amlog_binary((lg), (msg), (buf), (len)); }}

void amlog_hex(amlog_t* log, const char* msg, const void* buf, uint64_t length, uint64_t offset);
#define amlog_hex_mask(lg, lvl, cmp, msg, buf, len, off) { if (amlog_printable((lg), (lvl), (cmp))) { amlog_hex((lg), (msg), (buf), (len), (off)); }}

#endif
