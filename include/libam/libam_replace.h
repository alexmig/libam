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

#ifndef __FILENAME__
#define __FILENAME__		(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

/* __LINE__ in string form */
#ifndef __LINE_STRING__
#ifndef __LINE_STRING3__
#define __LINE_STRING3__(x)	#x
#endif
#ifndef __LINE_STRING2__
#define __LINE_STRING2__(x)	__LINE_STRING3__(x)
#endif
#define __LINE_STRING__		__LINE_STRING2__(__LINE__)
#endif

#ifndef __LOCATION__
#define __LOCATION__		__FILENAME__ ":" __LINE_STRING__
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
