#ifndef _LIBAM_HASH_H_
#define _LIBAM_HASH_H_

#include <stdint.h>

/* djb2 implementations hash functions */

uint64_t amhash(const uint8_t* data, uint64_t len);
uint64_t amshash(const char* str, uint64_t* out_len);
uint64_t amsnhash(const char* str, uint64_t max_len, uint64_t* out_len);

#endif
