#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int DEBUG;

static void debug(const char *msg);

int
main(int argc, char *argv[])
{
	char *debug_enabled = getenv("DEBUG_IO_MULTIPLEXING");
	DEBUG = debug_enabled
		&& strcmp(debug_enabled, "")
		&& strcmp(debug_enabled, "0");

	puts("Running forever, hit CTRL-C to stop.");
	debug("test");

	return 0;
}

static void
debug(const char *msg)
{
	if (DEBUG) {
		fprintf(stderr, "[parent][debug]: %s\n", msg);
	}
}

/*
my $script = __FILE__;
$script =~ s/parent\.pl$/child.pl/;

# This is the equivalent of FD_ZERO(&set) in C.
my $set = '';

foreach (1 .. 3) {
	# The variable $^X contains the path to the Perl interpreter.
	my ($pid, $child_stdout, $child_stderr) = spawn_child_process $^X, $script;

	my $child_stdout_fd = fileno $child_stdout;
	my $child_stderr_fd = fileno $child_stderr;

	debug "PID $pid has descriptors $child_stdout_fd and $child_stderr_fd\n";

	# Reading from the handle associated with the file descriptor must be
	# done with sysread() and not read() or the diamond operator <> so that
	# Perl's I/O buffering is bypassed.
	#
	# You must also make sure that you read the data in non-blocking mode
	# or alternatively just read one byte at a time.  The latter approach
	# is straightforward. So we go for the first one and set both descriptors
	# to non blocking I/O mode at system level.
	my $stdout_flags = fcntl $child_stdout, F_GETFL, 0
		or die "cannot get descriptor status flags of $child_stdout_fd: $!";
	fcntl $child_stdout, F_GETFL, $stdout_flags | O_NONBLOCK
		or die "cannot set descriptor $child_stdout_fd to non-blocking: $!";
	my $stderr_flags = fcntl $child_stderr, F_GETFL, 0
		or die "cannot get descriptor status flags of $child_stderr_fd: $!";
	fcntl $child_stderr, F_GETFL, $stderr_flags | O_NONBLOCK
		or die "cannot set descriptor $child_stderr_fd to non-blocking: $!";

	$descriptors{$child_stdout_fd} = {
		pid => $pid,
		prefix => 'info',
		buffer => '',
		handle => $child_stdout,
	};
	$descriptors{$child_stderr_fd} = {
		pid => $pid,
		prefix => 'error',
		buffer => '',
		handle => $child_stderr,
	};

	# Store the file descriptors in the set for select().
	vec($set, $child_stdout_fd, 1) = 1; # FD_SET(child_stdout, set) in C.
	vec($set, $child_stderr_fd, 1) = 1; # FD_SET(child_stdout, set) in C.
}

# Now poll all descriptors.
while (1) {
	# Now wait until any of these handles are ready to read s.  In C this would
	# be: select(&rout, NULL, NULL, NULL);
	select my $rout = $set, undef, undef, undef or next;

	foreach my $descriptor (keys %descriptors) {
		# Descriptor is a numerical file descriptor. Check whether the
		# corresponding bit is set in the $rout bitmask.
		next if !vec $rout, $descriptor, 1;
		debug "descriptor $descriptor is ready for reading";

		my $rec = $descriptors{$descriptor};
		my $offset = length $rec->{buffer};
		my $nread = sysread $rec->{handle}, $rec->{buffer}, 8192, $offset;

		if (!defined $nread) {
			debug "error reading from descriptor $descriptor: $!\n";
		} elsif ($nread == 0) {
			debug "end-of-file reading from descriptor $descriptor\n";

			# No point reading from that descriptor.
			vec($set, $descriptor, 1) = 0;
			delete $descriptors{$descriptor};
		}

		while ($rec->{buffer} =~ s/(.*?)\n//) {
			print "[child $rec->{pid}][$rec->{prefix}]: $1\n";
		}
	}
}

sub spawn_child_process {
	my ($perl, $script) = @_;

	pipe my $stdout_read, my $stdout_write or die "cannot create pipe: $!\n";
	pipe my $stderr_read, my $stderr_write or die "cannot create pipe: $!\n";

	my $pid = fork;
	if ($pid) {
		# Parent process.
		return $pid, $stdout_read, $stderr_read;
	} else {
		die "cannot fork: $!\n" if !defined $pid;

		# Child process.  Dup standard output and standard error to the pipe.
		open STDOUT, '>&', $stdout_write or die "cannot dup stdout: $!";
		open STDERR, '>&', $stderr_write or die "cannot dup stderr: $!";

		exec $perl, $script or die "cannot exec $perl $script: $!\n";
	}
}

sub debug {
	if (DEBUG) {
		my ($msg) = @_;

		chomp $msg;
		warn "[parent][debug]: $msg\n";
	}
}
*/
