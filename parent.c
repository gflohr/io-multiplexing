#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFSIZE 8192
#define CHILDREN 3

struct descriptor_info {
	int fd;
	pid_t pid;
	const char *prefix;
	char *buffer;
	size_t buffer_size;
};

static struct descriptor_info descriptors[CHILDREN << 1];
static int max_descriptor = -1;
static int terminating = 0;

static fd_set spawn_child_processes(const char *image);
static pid_t spawn_child_process(
	const char *image, int *child_stdout, int *child_stderr);
static void multiplex_io(fd_set *set);
static void process_output(struct descriptor_info *info, fd_set *set);
static char *xstrdup(const char *str);
static void *xrealloc(void *buf, size_t size);
static void reap_children(void);
static void sigchld_handler(int sig);
static void sigint_handler(int sig);

int
main(int argc, char *argv[])
{
	puts("Running forever, hit CTRL-C to stop.");

	memset(descriptors, 0, sizeof descriptors);

	struct sigaction sa;
	sa.sa_handler = &sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, 0) == -1) {
		fprintf(stderr, "cannot install sigchld handler: %s\n",
			strerror(errno));
		exit(1);
	}

	sa.sa_handler = &sigint_handler;
	if (sigaction(SIGINT, &sa, 0) == -1) {
		fprintf(stderr, "cannot install sigint handler: %s\n",
			strerror(errno));
		exit(1);
	}
	if (sigaction(SIGHUP, &sa, 0) == -1) {
		fprintf(stderr, "cannot install sighup handler: %s\n",
			strerror(errno));
		exit(1);
	}
	if (sigaction(SIGTERM, &sa, 0) == -1) {
		fprintf(stderr, "cannot install sigterm handler: %s\n",
			strerror(errno));
		exit(1);
	}
	if (sigaction(SIGQUIT, &sa, 0) == -1) {
		fprintf(stderr, "cannot install sigquit handler: %s\n",
			strerror(errno));
		exit(1);
	}
	
	sa.sa_handler = &sigint_handler;

	atexit(reap_children);

	fd_set set = spawn_child_processes("./child.exe");

	multiplex_io(&set);

	return 0;
}

static fd_set
spawn_child_processes(const char *image)
{

	fd_set set;
	FD_ZERO(&set);

	for (int i = 0; i < CHILDREN; ++i) {
		struct descriptor_info *stdout_info = &descriptors[i << 1];
		stdout_info->prefix = "info";
		stdout_info->buffer = xstrdup("");
		stdout_info->buffer_size = 1;
		struct descriptor_info *stderr_info = &descriptors[(i << 1) + 1];
		stderr_info->prefix = "error";
		stderr_info->buffer = xstrdup("");
		stderr_info->buffer_size = 1;

		stdout_info->pid = stderr_info->pid = spawn_child_process(
			"./child.exe", &stdout_info->fd, &stderr_info->fd
		);

		/* Because of the ideosyncrasies of select() we have to remember the
		 * highest file descriptor.
		 */
		if (stdout_info->fd > max_descriptor) {
			max_descriptor = stdout_info->fd;
		}
		if (stderr_info->fd > max_descriptor) {
			max_descriptor = stderr_info->fd;
		}

		/* You must make sure that you read the data in non-blocking mode
		 * or alternatively just read one byte at a time.  The latter approach
		 * is straightforward. So we go for the first one and set both
		 * descriptors to non blocking I/O mode at system level.
		 */
		int stdout_flags = fcntl(stdout_info->fd, F_GETFL, 0);
		if (stdout_flags < 0) {
			fprintf(
				stderr,
				"cannot get descriptor status flags of %u: %s\n",
				stdout_info->fd, strerror(errno)
			);
			exit(1);
		}
		if (fcntl(stdout_info->fd, F_SETFL, stdout_flags | O_NONBLOCK) < 0) {
			fprintf(
				stderr,
				"cannot set descriptor %u to non-blocking: %s\n",
				stdout_info->fd, strerror(errno)
			);
			exit(1);
		}
	
		int stderr_flags = fcntl(stderr_info->fd, F_GETFL, 0);
		if (stderr_flags < 0) {
			fprintf(
				stderr,
				"cannot get descriptor status flags of %u: %s\n",
				stderr_info->fd, strerror(errno)
			);
			exit(1);
		}
		if (fcntl(stderr_info->fd, F_SETFL, stderr_flags | O_NONBLOCK) < 0) {
			fprintf(
				stderr,
				"cannot set descriptor %u to non-blocking: %s\n",
				stderr_info->fd, strerror(errno)
			);
			exit(1);
		}

		/* Store the file descriptors in the set for select().  */
		FD_SET(stdout_info->fd, &set);
		FD_SET(stderr_info->fd, &set);
	}

	return set;
}

static pid_t
spawn_child_process(const char *image, int *stdout_read, int *stderr_read)
{
	int stdout_fd[2], stderr_fd[2];

	if (pipe(stdout_fd) != 0) {
		fprintf(stderr, "error creating pipe: %s\n", strerror(errno));
		exit(1);
	}

	if (pipe(stderr_fd) != 0) {
		fprintf(stderr, "error creating pipe: %s\n", strerror(errno));
		exit(1);
	}

	pid_t pid = fork();
	if (pid > 0) {
		*stdout_read = stdout_fd[0];
		*stderr_read = stderr_fd[0];

		return pid;
	} else if (pid < 0) {
		fprintf(stderr, "cannot fork: %s\n", strerror(errno));
	} else {
		if (dup2(stdout_fd[1], STDOUT_FILENO) < 0) {
			fprintf(stderr, "cannot dup stdout: %s\n", strerror(errno));
			exit(1);
		}

		if (dup2(stdout_fd[1], STDERR_FILENO) < 0) {
			fprintf(stderr, "cannot dup stderr: %s\n", strerror(errno));
			exit(1);
		}

		if (execl(image, NULL) < 0) {
			fprintf(stderr, "cannot execl %s: %s\n", image, strerror(errno));
			exit(1);
		}
	}

	return pid;
}

static void
multiplex_io(fd_set *set)
{
	unsigned num_descriptors = sizeof descriptors / sizeof descriptors[0];

	while (1) {
		fd_set rout;

		FD_COPY(set, &rout);
		if (select(max_descriptor + 1, &rout, NULL, NULL, NULL) < 0) {
			continue;
		}

		for (unsigned int i = 0; i < num_descriptors; ++i) {
			int fd = descriptors[i].fd;
			if (!FD_ISSET(fd, &rout)) {
				continue;
			}

			process_output(descriptors + i, set);
		}

		sleep(1);
	}
}

static void
process_output(struct descriptor_info *info, fd_set *set)
{
	char buffer[BUFSIZE];
	ssize_t bytes_read = read(info->fd, buffer, BUFSIZE);
	size_t offset = strlen(info->buffer);
	size_t needed = offset + bytes_read + 1;
	char *linefeed;

	if (bytes_read < 0) {
		fprintf(stderr, "error reading from child: %s\n", strerror(errno));
		exit(1);
	} else if (bytes_read == 0) {
		/* End-of-file.  */
		memset(info, 0, sizeof *info);
		FD_CLR(info->fd, set);
		return;
	}

	if (needed > info->buffer_size) {
		info->buffer = xrealloc(info->buffer, needed);
		info->buffer_size = needed;
	}

	strncat(info->buffer + offset, buffer, bytes_read);
	info->buffer[offset + bytes_read] = '\0';

	while ((linefeed = index(info->buffer, '\n'))) {
		*linefeed = '\0';
		printf(
			"[child %u][%s]: %s\n", info->pid, info->prefix, info->buffer
		);
		memmove(
			info->buffer, linefeed + 1, info->buffer_size - strlen(info->buffer)
		);
	}
}

static char *
xstrdup(const char *str)
{
	char *buffer = strdup(str);
	if (!buffer) {
		fprintf(stderr, "virtual memory exhausted!\n");
		exit(1);
	}

	return buffer;
}

static void *
xrealloc(void *ptr, size_t bytes)
{
	void *buffer = realloc(ptr, bytes);
	if (!buffer) {
		fprintf(stderr, "virtual memory exhausted!\n");
		exit(1);
	}

	return buffer;
}

static void
reap_children()
{
	if (terminating) {
		return;
	}

	fprintf(stderr, "waiting for children to terminate ...\n");

	for (int i = 0; i < CHILDREN; ++i) {
		struct descriptor_info *info = &descriptors[i << 1];
		if (!info->pid) {
			continue;
		}
		kill(info->pid, SIGKILL);
	}

	while (1) {
		int alive = 0;
		for (int i = 0; i < CHILDREN; ++i) {
			if (descriptors[i << 1].pid) {
				alive = 1;
				break;
			}
		}
		if (!alive) {
			fprintf(stderr, "all child processes terminated.\n");
			return;
		}
		sleep(1);
	}
}

static void
sigchld_handler(int sig)
{
	int saved_errno = errno;
	pid_t pid;
	while ((pid = waitpid((pid_t) (-1), 0, WNOHANG)) > 0) {
		for (int i = 0; i < CHILDREN; ++i) {
			if (descriptors[i << 1].pid == pid) {
				memset(&descriptors[i << 1], 0,
					sizeof descriptors[i << 1]
				);
				memset(&descriptors[(i << 1) + 1], 0,
					sizeof descriptors[(i << 1) + 1]
				);
			}
		}
	}

	errno = saved_errno;
}

static void sigint_handler(int sig)
{
	reap_children();
	terminating = 1;
	exit(1);
}