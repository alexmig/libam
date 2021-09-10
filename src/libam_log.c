#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>

#include "libam/libam_log.h"

const amlog_t default_log = { .level = UINT64_MAX, .component_mask = 0, .fd = 0 };
const amlog_t* dlog = &default_log;

void amlog_init(amlog_t* log, char* filename)
{
	if (log == NULL)
			return;
	memcpy(log, &default_log, sizeof(default_log));
	log->fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT);
	if (log->fd == -1) {
		AMLOG_PREFIX(&default_log, "ERROR: Unable to open file '%s' for logging\n", filename);
		log->fd = 0;
	}
}

void amlog_term(amlog_t* log)
{
	amlog_flush(log);
	close(log->fd);
	log->fd = 0;
}

// Printouts

void amlog_binary(amlog_t* log, const char* msg, const void* buf, const uint64_t length)
{
	uint64_t i;
	uint8_t* p = (uint8_t*)buf;

	if (msg != NULL)
		AMLOG_PREFIX(log, "%s, length %lu, ", msg, length);
	for (i = 0; i < length; i++)
		AMLOG_PLAIN(log, "%02x", p[i]);
	AMLOG_PLAIN(log, "\n");
}

void amlog_hex(amlog_t* log, const char* msg, const void* buf, uint64_t length, uint64_t offset)
{
	char hex[16*3 + 2], asc[16 + 2];
	const uint8_t* ptr = buf;
	const uint8_t* end = ptr + length;
	uint8_t skip = (offset & 0xF);
	uint64_t line_address = offset - skip;
	uint64_t i, hi, ai;

	if (msg != NULL)
		AMLOG_PREFIX(log, "%s, length %lu, ", msg, length);

	for (; line_address < offset + length; line_address += 16) {
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
		AMLOG_PLAIN(log, "  %06lx  %s %s\n", line_address, hex, asc);
	}
}
