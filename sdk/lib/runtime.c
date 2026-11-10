#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <rum/user.h>

int errno;
_Noreturn void exit(int status) { (void)fflush(NULL); rum_exit(status); }
_Noreturn void abort(void) { rum_exit(EXIT_FAILURE); }
