/* Doom's common type header includes this even when these calls are unused.
   Full-engine compilation/linking must still report the missing functions. */
#ifndef RUM_DOOM_PROBE_STRINGS_H
#define RUM_DOOM_PROBE_STRINGS_H
#include <stddef.h>
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
#endif
