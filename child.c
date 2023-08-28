#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int
main(int argc, char *argv[])
{
	pid_t pid = getpid();


	/* Make standard output and standard error line buffered.  */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	/* Try to force a different random seed for every child.  */
	srand(time(NULL) + pid);

	useconds_t us;

	while (1) {
		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stdout,
#if IS_UNIX
			"child pid %llu writing to stdout before sleeping %u us.\n",
#else
			"child pid %I64d writing to stdout before sleeping %u us.\n",
#endif
			pid, us);
		usleep(us);

		us = (int) (((float) rand() / RAND_MAX) * 3000000);
		fprintf(stderr,
#if IS_UNIX
			"child pid %llu writing to stderr before sleeping %u us.\n",
#else
			"child pid %I64d writing to stderr before sleeping %u us.\n",
#endif
			pid, us);
		usleep(us);
	}

	return 0;
}