#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
/* POSIX system.  */
# define IS_POSIX 1
# include <sys/select.h>
# include <sys/wait.h>
#else
/* MS-DOS */
# define IS_MS_DOS 1
# define FD_SETSIZE 1024
# include <winsock2.h>
# include <ws2tcpip.h>
# include <windows.h>
# include <io.h>

typedef struct ChildConnector {
	SOCKET socket;
	HANDLE pipe;
} ChildConnector;

# define socketpair(domain, type, protocol, sockets) \
	win32_socketpair(sockets)
static int win32_socketpair(SOCKET socks[2]);
static int convert_wsa_error_to_errno(int wsaerr);
static void create_pipe(HANDLE handles[]);
static DWORD create_watcher_thread(SOCKET socket, HANDLE pipe);
static WINAPI DWORD pipe_watcher(LPVOID args);
static void display_error(LPCTSTR msg);

#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#if IS_MS_DOS
# define PID_TYPE DWORD
#else
# define PID_TYPE pid_t
#endif

#define BUFSIZE 8192
#define CHILDREN 3

struct descriptor_info {
	int fd;
	PID_TYPE pid;
	const char *prefix;
	char *buffer;
	size_t buffer_size;
};

static struct descriptor_info descriptors[CHILDREN << 1];
static int max_descriptor = -1;

#if IS_POSIX
static int terminating = 0;
#endif

static fd_set spawn_child_processes(const char *image);
static PID_TYPE spawn_child_process(char *image, int *fd);
static void multiplex_io(fd_set *set);
static void process_output(struct descriptor_info *info, fd_set *set);
static char *xstrdup(const char *str);
static void *xrealloc(void *buf, size_t size);
#if IS_POSIX
static void reap_children(void);
static void sigchld_handler(int sig);
static void sigint_handler(int sig);
#endif

#if IS_MS_DOS
/* Taken from Perl's win32/win32sck.c but returns EINVAL by default.  */
static int
convert_wsa_error_to_errno(int wsaerr)
{
    switch (wsaerr) {
    case WSAEINTR:
        return EINTR;
    case WSAEBADF:
        return EBADF;
    case WSAEACCES:
        return EACCES;
    case WSAEFAULT:
        return EFAULT;
    case WSAEINVAL:
        return EINVAL;
    case WSAEMFILE:
        return EMFILE;
    case WSAEWOULDBLOCK:
        return EWOULDBLOCK;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAENOTSOCK:
        return ENOTSOCK;
    case WSAEDESTADDRREQ:
        return EDESTADDRREQ;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAEPROTOTYPE:
        return EPROTOTYPE;
    case WSAENOPROTOOPT:
        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:
        return EOPNOTSUPP;
    case WSAEAFNOSUPPORT:
        return EAFNOSUPPORT;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAENETRESET:
        return ENETRESET;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAENOBUFS:
        return ENOBUFS;
    case WSAEISCONN:
        return EISCONN;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAESHUTDOWN:
		return ECONNRESET;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAELOOP:
        return ELOOP;
    case WSAENAMETOOLONG:
        return ENAMETOOLONG;
    case WSAEHOSTDOWN:
        return ENETDOWN;        /* EHOSTDOWN is not defined */
    case WSAEHOSTUNREACH:
        return EHOSTUNREACH;
    case WSAENOTEMPTY:
        return ENOTEMPTY;
    case WSAEPROCLIM:
        return EAGAIN;
    case WSAEUSERS:
        return EAGAIN;
    case WSAEDQUOT:
        return EAGAIN;
#ifdef WSAECANCELLED
    case WSAECANCELLED:         /* New in WinSock2 */
        return ECANCELED;
#endif
    }

    return EINVAL;
}

/* This is inspired by the socketpair() emulation of Perl.  */
static int
win32_socketpair(SOCKET sockets[2])
{
    SOCKET listener = SOCKET_ERROR;
	struct sockaddr_in listener_addr;
	SOCKET connector = SOCKET_ERROR;
	struct sockaddr_in connector_addr;
	SOCKET acceptor = SOCKET_ERROR;
    int saved_errno;
    socklen_t addr_size;

    if (!sockets) {
		errno = EINVAL;
		return SOCKET_ERROR;
    }

    listener = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
    if (listener < 0) {
        return SOCKET_ERROR;
	}

    memset(&listener_addr, 0, sizeof listener_addr);
	listener_addr.sin_family = AF_INET;
    listener_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener_addr.sin_port = 0;  /* OS picks a random free port.  */

	errno = 0;
	if (bind(listener, (struct sockaddr *) &listener_addr,
		sizeof listener_addr) == -1) {
		goto fail_win32_socketpair;
	}

	if (listen(listener, 1) < 0) {
		goto fail_win32_socketpair;
	}

	/* This must be WSASocket() and not socket().  The subtle difference is
	 * that only sockets created by WSASocket() can be used as standard
	 * file descriptors.
	 */
	connector = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
	if (connector == -1) {
		goto fail_win32_socketpair;
	}

	/* Get the port number.  */
	addr_size = sizeof connector_addr;
	if (getsockname(listener, (struct sockaddr *) &connector_addr,
	                &addr_size) < 0) {
		goto fail_win32_socketpair;
	}
	if (addr_size != sizeof connector_addr) {
		goto abort_win32_socketpair;
	}

	if (connect(connector, (struct sockaddr *) &connector_addr,
	            addr_size) < 0) {
		goto fail_win32_socketpair;
	}

	acceptor = accept(listener, (struct sockaddr *) &listener_addr, &addr_size);
	if (acceptor < 0) {
		goto fail_win32_socketpair;
	}
	if (addr_size != sizeof listener_addr) {
		goto abort_win32_socketpair;
	}

    closesocket(listener);

	/* The port and host on the socket must be identical.  */
	if (getsockname(connector, (struct sockaddr *) &connector_addr,
	                &addr_size) < 0) {
		goto fail_win32_socketpair;
	}
	
	if (addr_size != sizeof connector_addr
		|| listener_addr.sin_family != connector_addr.sin_family
		|| listener_addr.sin_addr.s_addr != connector_addr.sin_addr.s_addr
		|| listener_addr.sin_port != connector_addr.sin_port) {
		goto abort_win32_socketpair;
	}

	sockets[0] = connector;
	sockets[1] = acceptor;
	return 0;

abort_win32_socketpair:
#ifdef ECONNABORTED
  errno = ECONNABORTED; /* This would be the standard thing to do. */
#elif defined(ECONNREFUSED)
  errno = ECONNREFUSED; /* some OSes might not have ECONNABORTED. */
#else
  errno = ETIMEDOUT;    /* Desperation time. */
#endif

fail_win32_socketpair:
	if (!errno) {
		errno = convert_wsa_error_to_errno(WSAGetLastError());
	}

	saved_errno = errno;
	if (listener >= 0) {
    	closesocket(listener);
	}
	if (connector >= 0) {
    	closesocket(connector);
	}
	if (acceptor >= 0) {
    	closesocket(acceptor);
	}
	errno = saved_errno;

    return SOCKET_ERROR;
}
#endif

int
main(int argc, char *argv[])
{
	puts("Running forever, hit CTRL-C to stop.");

#if IS_MS_DOS
	/* Networking has to be initialized on MS-DOS.  Alternatively, add the
	 * calls to WSAStartup()/WSACleanup() to win32_socketpair() as they
	 * can apparently be nested.
	 */
    WORD versionRequested;
    WSADATA wsaData;
    int err;

    versionRequested = MAKEWORD(2, 2);

    err = WSAStartup(versionRequested, &wsaData);
    if (err != 0) {
        fprintf(stderr, "WSAStartup failed with error code: %d\n", err);
        return 1;
    }

	/* Request version 2.2.  */
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        fprintf(stderr, "could not find a usable version of Winsock.dll\n");
        WSACleanup();
        return 1;
    }  
#endif

	memset(descriptors, 0, sizeof descriptors);

#if IS_POSIX
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
#endif

#if IS_MSDOS
	fd_set set = spawn_child_processes(".\\child.exe");
#else
	fd_set set = spawn_child_processes("./child.exe");
#endif
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
		int fd[2];

		stdout_info->pid = stderr_info->pid = spawn_child_process(
			"./child.exe", fd
		);
		stdout_info->fd = fd[0];
		stderr_info->fd = fd[1];

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
#if IS_MS_DOS
		u_long nb_mode = 1;
		ioctlsocket(stdout_info->fd, FIONBIO, &nb_mode);
		ioctlsocket(stderr_info->fd, FIONBIO, &nb_mode);
#else
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
#endif

		/* Store the file descriptors in the set for select().  */
		FD_SET(stdout_info->fd, &set);
		FD_SET(stderr_info->fd, &set);
	}

	return set;
}

static PID_TYPE
spawn_child_process(char *cmd, int *fd)
{
#if IS_MS_DOS
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
	SOCKET stdout_sockets[2] = { SOCKET_ERROR, SOCKET_ERROR };
	SOCKET stderr_sockets[2] = { SOCKET_ERROR, SOCKET_ERROR };
	SOCKET selectable_stdout;
	SOCKET selectable_stderr;
	BOOL created;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, stdout_sockets) < 0) {
        fprintf(stderr, "cannot create socketpair: %s\n", strerror(errno));
        goto create_process_failed;
	}
	selectable_stdout = stdout_sockets[1];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, stderr_sockets) < 0) {
       	fprintf(stderr, "cannot create socketpair: %s\n", strerror(errno));
        goto create_process_failed;
	}
	selectable_stderr = stderr_sockets[1];

	/* Create two pipes for stdout and stderr.  */
	HANDLE child_stdout[2];
	create_pipe(child_stdout);

    HANDLE child_stderr[2];
	create_pipe(child_stderr);
	
	/* We wait for child output until we receive a signal.  If you want
	 * to catch output only for a limited time, you have to remember the
	 * threads created, and wait for them to terminate.
	 */
	(void) create_watcher_thread(selectable_stdout, child_stdout[0]);
	(void) create_watcher_thread(selectable_stderr, child_stderr[0]);

    memset(&si, 0, sizeof(si));	
    si.cb = sizeof(si);
	si.hStdOutput = child_stdout[1];
	si.hStdError = child_stderr[1];
	si.dwFlags = STARTF_USESTDHANDLES;

    memset(&pi, 0, sizeof(pi));

	created = CreateProcess(NULL,
        cmd,            // Command.
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        TRUE,           // Set handle inheritance to FALSE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi             // Pointer to PROCESS_INFORMATION structure
    );

	if (!created) {
        printf("CreateProcess failed: %s.\n", strerror(errno));
        goto create_process_failed;
    }

	fd[0] = stdout_sockets[0];
	fd[1] = stderr_sockets[0];

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	//close(child_stdout[1]);
	//close(child_stderr[1]);

    return pi.dwProcessId;

create_process_failed:
	if (stdout_sockets[0] >= 0) {
		closesocket(stdout_sockets[0]);
	}

	if (stdout_sockets[1] >= 0) {
		closesocket(stdout_sockets[1]);
	}

	if (stderr_sockets[0] >= 0) {
		closesocket(stderr_sockets[0]);
	}

	if (stderr_sockets[1] >= 0) {
		closesocket(stderr_sockets[1]);
	}

	if (pi.hProcess) {
		CloseHandle(pi.hProcess);
	}

	if (pi.hThread) {
		CloseHandle(pi.hThread);
	}

	return -1;
#else
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

		if (execl(cmd, NULL) < 0) {
			fprintf(stderr, "cannot execl %s: %s\n", cmd, strerror(errno));
			exit(1);
		}
	}

	return pid;
#endif
}

#if IS_MS_DOS
static void
create_pipe(HANDLE handles[2])
{
	SECURITY_ATTRIBUTES sa;

	sa.nLength = sizeof sa;
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	/* Microsoft says that _pipe() is not available for applications that
	 * execute in the "Windows Runtime" whatever that is, see
	 * https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/pipe
	 * 
	 * If you run into issues with _pipe(), use the method CreatePipe()
	 * instead which creates the obscure HANDLE objects.  Keep in mind that
	 * you then cannot inquire errno for errors but have to use the wonky
	 * MS-DOS error reporting with GetLastError().
	 */
    if (!CreatePipe(&handles[0], &handles[1], &sa, 0)) {
		display_error(TEXT("cannot create pipe"));
    	exit(1);
    }

   /* Necessary according to Microsoft documentation, see
    * https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
	* I could not see any effect, though.
	*/ 
   	if (!SetHandleInformation(handles[0], HANDLE_FLAG_INHERIT, 0)) {
      	display_error(TEXT("cannot unset inherited flag on child stdout"));
		exit(1);
	}
}

static DWORD
create_watcher_thread(SOCKET socket, HANDLE pipe)
{
	DWORD thread;
	ChildConnector *conn;

	conn = (ChildConnector *) HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, sizeof(ChildConnector));
	conn->socket = socket;
	conn->pipe = pipe;

	if (NULL == CreateThread(NULL, 0, pipe_watcher, conn, 0, &thread)) {
		display_error(TEXT("cannot create thread: "));
		ExitProcess(1);
	}

	return thread;
}

static DWORD WINAPI
pipe_watcher (LPVOID args)
{
	ChildConnector *comm = (ChildConnector *) args;
	char rbuf[BUFSIZE];

	while (1) {
		DWORD bytes_read;
   		BOOL status = ReadFile(comm->pipe, rbuf, BUFSIZE, &bytes_read, NULL);
		if (!status) {
			/* Microsoft states in its nebulous documenation for _read() that
			 * errno is set to EBADF "if execution is allowed to continue".
			 * Does that mean that the error should be ignored? No idea. :(
			 */
			display_error(TEXT("read from pipe failed"));
			return 1;
		} else if (bytes_read == 0) {
			fprintf(stderr, "end of file reading from child process");
			return 1;
		} else {
			if (send(comm->socket, rbuf, bytes_read, 0) < 0) {
				errno = convert_wsa_error_to_errno(WSAGetLastError());
				fprintf(stderr, "send failed: %s\n", strerror(errno));
				return 1;
			}
		}
	}

	return 0;
}

static void
display_error(LPCTSTR msg)
{
	// Retrieve the system error message for the last-error code.
    LPVOID lpMsgBuf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL
	);

	fprintf(stderr, "%s: %s\n", msg, (LPTSTR) &lpMsgBuf);

    LocalFree(lpMsgBuf);
}
#endif

static void
multiplex_io(fd_set *set)
{
	unsigned num_descriptors = sizeof descriptors / sizeof descriptors[0];

	while (1) {
		fd_set rout;

#ifdef FD_COPY
		FD_COPY(set, &rout);
#else
		rout = *set;
#endif

		if (select(max_descriptor + 1, &rout, NULL, NULL, NULL) < 0) {
		fprintf(stderr, "select error :(\n");
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
#if IS_MS_DOS
	ssize_t bytes_read = recv(info->fd, buffer, BUFSIZE, 0);
#else
	ssize_t bytes_read = read(info->fd, buffer, BUFSIZE);
#endif
	size_t offset = strlen(info->buffer);
	size_t needed = offset + bytes_read + 1;
	char *linefeed;

	if (bytes_read < 0) {
#if IS_MS_DOS
		errno = convert_wsa_error_to_errno(WSAGetLastError());
#endif
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

	while ((linefeed = strchr(info->buffer, '\n'))) {
		*linefeed = '\0';
		printf(
#if IS_POSIX
			"[child %u][%s]: %s\n", info->pid, info->prefix, info->buffer
#else
			"[child %lu][%s]: %s\n", info->pid, info->prefix, info->buffer
#endif
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

#if IS_POSIX
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
#endif
