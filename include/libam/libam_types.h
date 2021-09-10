#ifndef _LIBAM_TYPES_H_
#define _LIBAM_TYPES_H_

#include <stdint.h>

typedef enum amrc {
	AMRC_SUCCESS,
	AMRC_ERROR,
} amrc_t;

typedef enum ambool {
	am_false = (0 == 1),
	am_true = !am_false,
} ambool_t;

#endif
