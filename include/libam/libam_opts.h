#ifndef _LIBAM_OPTS_H_
#define _LIBAM_OPTS_H_

#include <stdint.h>

#include "libam_types.h"
#include "libam_list.h"

typedef enum amopt_type {
	AMOPT_FLAG,	// Represented as uint8_t
	AMOPT_UINT64,
	AMOPT_UDOUBLE, // Positive double
	AMOPT_STRING,
	AMOPT_CUSTOM,
} amopt_type_t;

typedef amrc_t (*amopt_val_t)(void* opt, void* member);
typedef amrc_t (*amopt_prase_t)(void* opt, void* member, const char* input);

/* Option descriptor
 * short, long, Help, type, default, offset, validate, parse, print
 *
 * Constraints:
 * 1. Must have long.
 * 2. All names must be printable, no spaces
 * 3. No default = mandatory argument
 */
typedef struct amopt_option {
	char		form_short;	// In the form of -c
	char*		form_long;	// In the form of --str
	char*		help_string;
	amopt_type_t type;
	char*		default_value;
	uint64_t	offset;
	amopt_val_t	validate;
	amopt_prase_t parse;
	amopt_val_t	print_custom;

	// Internal:
	amlink_t	link;
	uint8_t		is_set:1;
} amopt_option_t;

typedef struct amopt {
	amlist_t	opt_list;
	void*		structure;
	amopt_val_t	final_val;
} amopt_t;

void amopts_init(amopt_t* opt, void* structure, amopt_val_t final_validate);
amrc_t amopts_register_option(amopt_t* opt, amopt_option_t* option);

amrc_t amopts_read(amopt_t* opt, int argc, char** argv);

void amopts_print_help(amopt_t* opt);
void amopts_print_values(amopt_t* opt);

#endif
