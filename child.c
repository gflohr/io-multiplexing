#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int
main(int argc, char *argv[])
{
	pid_t pid = getpid();

	/* Make standard output and standard error unbuffered.  Normally, you
	 * make them line buffered but that doesn't work on MS-DOS.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	/* Try to force a different random seed for every child.  */
	srand(time(NULL) + pid);

	useconds_t us;

	while (1) {
		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stdout,
#if IS_UNIX
			"C child pid %llu writing to stdout before sleeping %u us.\n",
#else
			"C child pid %I64d writing to stdout before sleeping %u us.\n",
#endif
			pid, us);
		usleep(us);

		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stderr,
#if IS_UNIX
			"C child pid %llu writing to stderr before sleeping %u us.\n",
#else
			"C child pid %I64d writing to stderr before sleeping %u us.\n",
#endif
			pid, us);
		usleep(us);
	}

	return 0;
}