#include <stdlib.h>
#include "libam/libam_hash.h"

/* Source: http://www.cse.yorku.ca/~oz/hash.html, djb2 */
uint64_t amhash(const uint8_t* data, uint64_t len)
{
	uint64_t hash = 5381;
	uint8_t c;

	while (len > 0) {
		c = *data;
		data++;
		len--;
		hash += (hash << 5) + c;
	}
	return hash;
}

uint64_t amshash(const char* str, uint64_t* out_len)
{
	uint64_t hash = 5381;
	uint64_t len = 0;
	char c;

	while ((c = *str++)) {
		hash += (hash << 5) + c;
		len++;
	}

	if (out_len != NULL)
		*out_len = len;
	return hash;
}

uint64_t amsnhash(const char* str, uint64_t max_len, uint64_t* out_len)
{
	uint64_t hash = 5381;
	uint64_t len;
	char c;

	for (len = 0; len < max_len; len++) {
		c = *str++;
		if (c == '\0')
			break;
		hash += (hash << 5) + c;
	}

	if (out_len != NULL)
		*out_len = len;
	return hash;
}
