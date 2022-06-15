#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "libam/libam_opts.h"
#include "libam/libam_log.h"

static int initialized = 0;
static int is_problem[UINT8_MAX];

#define UNUSED(x) (void)(x)

#ifdef DEBUG
#include "libam/libam_logsink.h"
#define DEBUG_PRINT(fmt, args...)	amlog_sink_log(0, 0, fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif

void amopts_init(amopt_t* opt, void* structure, amopt_val_t final_validate)
{
	if (!initialized) {
		initialized = 1;
		memset(is_problem, 0, sizeof(is_problem));
		for (int i = 1; i < UINT8_MAX; i++)
			is_problem[i] = !isprint((char)i);
		is_problem['\n'] = 1;
		is_problem['\"'] = 1;
		is_problem['\''] = 1;
		is_problem['='] = 1;
	}

	assert(opt != NULL);
	assert(structure != NULL);

	amlist_init(&opt->opt_list);
	opt->final_val = final_validate;
	opt->structure = structure;
}

static inline amrc_t amopts_validate_char(char c, int is_space_ok)
{
	if (c == '\0')
		return AMRC_SUCCESS;
	if (is_problem[(int)c])
		return AMRC_ERROR;
	if (!is_space_ok && isspace(c))
		return AMRC_ERROR;
	return AMRC_SUCCESS;
}

static amrc_t amopts_validate_printable(const char* str, int is_space_ok)
{
	if (str == NULL)
		return AMRC_SUCCESS;

	while (1) {
		if (*str == '\0')
			return AMRC_SUCCESS;
		if (amopts_validate_char(*str, is_space_ok) != AMRC_SUCCESS)
			return AMRC_ERROR;
		str++;
	}
}

static amrc_t amopts_validate_numeric(const char* str, int is_negative_okay, int is_dot_ok)
{
	uint8_t got_digit = 0;
	if (is_negative_okay && *str == '-')
		str++;

	while (1) {
		if (*str == '\0')
			return (got_digit ? AMRC_SUCCESS : AMRC_ERROR);

		if (is_dot_ok && *str == '.') {
			str++;
			is_dot_ok = 0;
			continue;
		}

		if (!isdigit(*str))
			return AMRC_ERROR;
		got_digit = 1;
		str++;
	}
}

static amrc_t amopts_validate(const amopt_option_t* opt)
{
	// Validate individual ones

	if (amopts_validate_char(opt->form_short, 0) != AMRC_SUCCESS) {
		DEBUG_PRINT("ERROR: Option %p has non-printable short form\n", opt);
		return AMRC_ERROR;
	}

	if (opt->form_long == NULL || opt->form_long[0] == '\0') {
		DEBUG_PRINT("ERROR: Must provide long-form name of option\n");
	}

	if (amopts_validate_printable(opt->form_long, 0) != AMRC_SUCCESS) {
		DEBUG_PRINT("ERROR: Option %p has non-printable long form\n", opt);
		return AMRC_ERROR;
	}

	if (strlen(opt->form_long) >= 20) {
		DEBUG_PRINT("ERROR: Option %s has too long of a form\n", opt->form_long);
		return AMRC_ERROR;
	}

	if (amopts_validate_printable(opt->help_string, 1) != AMRC_SUCCESS) {
		DEBUG_PRINT("ERROR: Option %s has non-printable help string\n", opt->form_long);
		return AMRC_ERROR;
	}

	switch (opt->type) {
	case AMOPT_FLAG:
	case AMOPT_UINT64:
	case AMOPT_UDOUBLE:
	case AMOPT_STRING:
	case AMOPT_CUSTOM:
		break;
	default:
		DEBUG_PRINT("ERROR: Unrecognized option type %d\n", opt->type);
		return AMRC_ERROR;
	}

	if (amopts_validate_printable(opt->default_value, 1) != AMRC_SUCCESS) {
		DEBUG_PRINT("ERROR: Option %p has non-printable default value\n", opt);
		return AMRC_ERROR;
	}

	// Validate combinations
	if (opt->type == AMOPT_CUSTOM && opt->parse == NULL) {
		DEBUG_PRINT("ERROR: Must provide parsing callback for custom option %p\n", opt);
		return AMRC_ERROR;
	}

	if (opt->type != AMOPT_CUSTOM && opt->parse != NULL) {
		DEBUG_PRINT("ERROR: Cannot provide custom parsing for standard types\n");
		return AMRC_ERROR;
	}

	if (opt->type == AMOPT_FLAG && opt->validate != NULL) {
		DEBUG_PRINT("ERROR: Flag options do not support validation\n");
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

static amrc_t amopt_prase_flag(void* opt, void* member, const char* input)
{
	uint8_t* flag_dest = member;
	UNUSED(opt);
	UNUSED(input);

	*flag_dest = 1;
	return AMRC_SUCCESS;
}

static amrc_t amopt_prase_uint64(void* opt, void* member, const char* input)
{
	uint64_t* dest = member;
	UNUSED(opt);

	if (amopts_validate_numeric(input, 0, 0) != AMRC_SUCCESS) {
		printf("ERROR: Error parsing input '%s', was expecting positive number\n", input);
		return AMRC_ERROR;
	}

	if (sscanf(input, "%lu", dest) != 1) {
		printf("ERROR: Unable to read input '%s'\n", input);
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

static amrc_t amopt_prase_udouble(void* opt, void* member, const char* input)
{
	double* dest = member;
	UNUSED(opt);

	if (amopts_validate_numeric(input, 0, 1) != AMRC_SUCCESS) {
		printf("ERROR: Error parsing input '%s', was expecting positive number\n", input);
		return AMRC_ERROR;
	}

	if (sscanf(input, "%lf", dest) != 1) {
		printf("ERROR: Unable to read input '%s'\n", input);
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

static amrc_t amopt_prase_string(void* opt, void* member, const char* input)
{
	char** dest = member;
	char* dup = strdup(input);
	UNUSED(opt);

	if (dup == NULL) {
		printf("ERROR: Failed to duplicate '%s'\n", input);
		return AMRC_ERROR;
	}

	*dest = dup;
	return AMRC_SUCCESS;
}

amrc_t amopts_register_option(amopt_t* opt, amopt_option_t* opn)
{
	amrc_t rc;
	void* dest = ((uint8_t*)opt->structure) + opn->offset;

	rc = amopts_validate(opn);
	if (rc != AMRC_SUCCESS)
		return AMRC_ERROR;

	switch (opn->type) {
	case AMOPT_FLAG:	opn->parse = amopt_prase_flag; break;
	case AMOPT_UINT64:	opn->parse = amopt_prase_uint64; break;
	case AMOPT_UDOUBLE:	opn->parse = amopt_prase_udouble; break;
	case AMOPT_STRING:	opn->parse = amopt_prase_string; break;
	case AMOPT_CUSTOM:	break;
	}

	opn->is_set = 0;
	if (opn->type == AMOPT_FLAG) {
		*((uint8_t*)dest) = (opn->default_value == NULL ? 0 : 1);
		opn->is_set = 1;
	}
	else if (opn->default_value != NULL) {
		rc = opn->parse(opt->structure, dest, opn->default_value);
		if (rc != AMRC_SUCCESS) {
			printf("ERROR: Option %s failed to parse default value\n", opn->form_long);
			return AMRC_ERROR;
		}
		opn->is_set = 1;
	}

	amlist_add_tail(&opt->opt_list, &opn->link);
	return AMRC_SUCCESS;
}

static amopt_option_t* amopts_search_long(amopt_t* opt, char* name)
{
	amopt_option_t* opn;

	amlist_for_each_entry(opn, &opt->opt_list, link) {
		if (strcmp(name, opn->form_long) == 0)
			return opn;
	}

	return NULL;
}

static amopt_option_t* amopts_search_short(amopt_t* opt, char* name)
{
	amopt_option_t* opn;

	if (name[0] == '\0' || name[1] != '\0')
		return NULL;

	amlist_for_each_entry(opn, &opt->opt_list, link) {
		if (*name == opn->form_short)
			return opn;
	}

	return NULL;
}

static amrc_t amopts_read_value(amopt_t* opt, amopt_option_t* opn, char* value)
{
	amrc_t rc;
	void* dest = ((uint8_t*)opt->structure) + opn->offset;

	rc = opn->parse(opt->structure, dest, value);
	if (rc != AMRC_SUCCESS) {
		printf("ERROR: Failed to parse input '%s' to option %s\n", value, opn->form_long);
		return AMRC_ERROR;
	}

	if (opn->validate != NULL) {
		rc = opn->validate(opt->structure, dest);
		if (rc != AMRC_SUCCESS) {
			return AMRC_ERROR;
		}
	}

	opn->is_set = 1;

	return AMRC_SUCCESS;
}

amrc_t amopts_read(amopt_t* opt, int argc, char** argv)
{
	amopt_option_t* opn;
	void* dest;
	char* arg;
	amrc_t rc;

	// Parse & validate individual arguments
	for (int i = 0; i < argc; i++) {
		arg = argv[i];

		opn = NULL;
		if (arg[0] == '-' && arg[1] == '-')
			opn = amopts_search_long(opt, arg + 2);
		else  if (arg[0] == '-')
			opn = amopts_search_short(opt, arg + 1);

		if (opn == NULL) {
			printf("ERROR: Unrecognized option '%s'\n", arg);
			return AMRC_ERROR;
		}

		dest = ((uint8_t*)opt->structure) + opn->offset;
		if (opn->type == AMOPT_FLAG) {
			*((uint8_t*)dest) = 1;
			continue;
		}

		if (i + 1 >= argc) {
			printf("ERROR: Must provide input to argument %s\n", opn->form_long);
			return AMRC_ERROR;
		}

		rc = amopts_read_value(opt, opn, argv[i + 1]);
		if (rc != AMRC_SUCCESS)
			return AMRC_ERROR;

		i++;

		continue;
	}

	amlist_for_each_entry(opn, &opt->opt_list, link) {
		if (!opn->is_set) {
			printf("ERROR: No default exists for option %s, must specify a value\n", opn->form_long);
			return AMRC_ERROR;
		}
	}

	rc = AMRC_SUCCESS;
	if (opt->final_val != NULL)
		rc = opt->final_val(opt->structure, NULL);

	return (rc == AMRC_SUCCESS ? AMRC_SUCCESS : AMRC_ERROR);
}

static int amopts_print_opt(amopt_option_t* opn)
{
	const int opts_len = 30;
	const int max_help = 112 - opts_len;
	int len;
	const char* help = opn->help_string;
	int is_mandatory = 0;

	if (opn->default_value == NULL && opn->type != AMOPT_FLAG) {
		printf("*");
		is_mandatory = 1;
	}
	else
		printf(" ");

	if (opn->form_short != '\0')
		printf("-%c ", opn->form_short);
	else
		printf("   ");
	printf("--%-20s ", opn->form_long);

	len = strnlen(opn->help_string, 256);

	while (len > 0) {
		if (len <= max_help) {
			printf("%.*s\n", len, help);
			return is_mandatory;
		}

		printf("%.*s\n", max_help, help);
		printf("                           ");
		help += max_help;
		len -= max_help;
	}
	printf("\n");
	return is_mandatory;
}


void amopts_print_help(amopt_t* opt)
{
	amopt_option_t* option;
	int mandatories = 0;
	amlist_for_each_entry(option, &opt->opt_list, link) {
		mandatories = amopts_print_opt(option) || mandatories;
	}
	printf("\n");
	if (mandatories)
		printf(" Options marked with '*' are mandatory.\n");
	printf(" Specify value --[option] [value]. Flags require no value.\n\n");
}

void amopts_print_values(amopt_t* opt)
{
	amopt_option_t* opn;
	void* dest;

	printf("Printing command-line options\n");
	amlist_for_each_entry(opn, &opt->opt_list, link) {
		dest = ((uint8_t*)opt->structure) + opn->offset;

		printf("  %-20s ", opn->form_long);
		switch (opn->type) {
		case AMOPT_FLAG: 	printf("%u\n", *(uint8_t*)dest); break;
		case AMOPT_UINT64: 	printf("%lu\n", *(uint64_t*)dest); break;
		case AMOPT_UDOUBLE: printf("%lf\n", *(double*)dest); break;
		case AMOPT_STRING: 	printf("'%s'\n", *((char**)dest)); break;
		case AMOPT_CUSTOM:
			if (opn->print_custom != NULL) opn->print_custom(opt->structure, dest);
			else printf("Custom attribute with no print function\n");
			break;
		}
	}
}
