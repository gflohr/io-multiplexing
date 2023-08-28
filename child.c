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
		us = rand() % 3000000;
		fprintf(stdout,
			"child pid %u writing to stdout before sleeping %u μs.\n",
			pid, us);
		usleep(us);

		us = rand() % 3000000;
		fprintf(stderr,
			"child pid %u writing to stderr before sleeping %u μs.\n",
			pid, us);
		usleep(us);
	}

	return 0;
}