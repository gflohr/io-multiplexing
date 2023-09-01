#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
/* POSIX system.  */
#define BUFMODE _IOLBF
#else
/* MS-DOS */
# define IS_MS_DOS 1

/* Trying to make stdout and stderr line-buffered does not work on MS-DOS.
 * Therefore make it unbuffered.
 */
#define BUFMODE _IONBF
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int
main(int argc, char *argv[])
{
	pid_t pid = getpid();

	setvbuf(stdout, NULL, BUFMODE, 0);
	setvbuf(stderr, NULL, BUFMODE, 0);

	/* Try to force a different random seed for every child.  */
	srand(time(NULL) + pid);

	useconds_t us;

	while (1) {
		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stdout,
		/* The MS-DOS crap shell does not support UTF-8.  */
#if IS_MS_DOS
			"C child pid %I64d writing to stdout before sleeping %us us.\n",
#else
			"C child pid %d writing to stdout before sleeping %u μs.\n",
#endif
			pid, us);
		usleep(us);

		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stderr,
#if IS_MS_DOS
			"C child pid %I64d writing to stderr before sleeping %u us.\n",
#else
			"C child pid %d writing to stderr before sleeping %u μs.\n",
#endif
			pid, us);
		usleep(us);
	}

	return 0;
}