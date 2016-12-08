#ifndef LONGHORN_LOG_HEADER
#define LONGHORN_LOG_HEADER

#define errorf(fmt, args...)					\
do {									\
	fprintf(stderr, "%s: " fmt, __FUNCTION__, ##args);		\
} while (0)

#endif
