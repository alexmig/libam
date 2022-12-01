#ifndef _LIBAM_REPLACE_H_
#define _LIBAM_REPLACE_H_

#include <string.h>

#ifndef _MIN
#define _MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

#ifndef _MAX
#define _MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))
#endif

#ifndef __LOCATION__
#ifndef __FLSTRING1__
#ifndef __FLSTRING2__
#define __FLSTRING2__(file, line) file ":" #line
#endif
#define __FLSTRING1__(file, line) __FLSTRING2__(file, line)
#endif

#ifndef __FILE_NAME__
#ifndef __FLSTRING__
#define __FLSTRING__ __FLSTRING1__(__FILE__, __LINE__)
#endif

#define __FILE_NAME__		(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define __LOCATION__		(strrchr(__FLSTRING__, '/') ? strrchr(__FLSTRING__, '/') + 1 : __FLSTRING__)

#else /* __FILE_NAME__ exists */

#define __LOCATION__		__FLSTRING1__(__FILE_NAME__, __LINE__)
#endif
#endif


#if (__GNUC__ >= 3)
#ifndef LIKELY
#define LIKELY(x)			__builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x)			__builtin_expect(!!(x), 0)
#endif
#else
#ifndef LIKELY
#define LIKELY(x)			 (x)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x)			 (x)
#endif
#endif

#ifndef PUBLIC
#ifdef HAVE_VISIBILITY_ATTR
#define PUBLIC __attribute__((visibility("default")))
#else
#define PUBLIC
#endif
#endif

#ifndef is_power_of_two
#define is_power_of_two(num) (((num) != 0) && !((num) & ((num) - 1)))
#endif

#ifndef discard_const
/* To avoid warnings and easier tracking throught the code. Use carefully */
#define discard_const(ptr) ((void *)(ptr))
#endif

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

#endif /* _LIBAM_REPLACE_H_ */
