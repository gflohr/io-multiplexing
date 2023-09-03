#! /usr/bin/env perl

use strict;

use Fcntl;
use Socket; # For MS-DOS only.

sub spawn_child_process;
sub spawn_child_process_msdos;
sub debug;

use constant DEBUG => $ENV{DEBUG_IO_MULTIPLEXING};

# MS-DOS ioctl for making a socket unbuffered.
use constant FIONBIO => 0x8004667e;

print "Running forever, hit CTRL-C to stop.\n";

my %descriptors;

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
	if ('MSWin32' eq $^O) {
		my $true = 1;
		ioctl $child_stdout, FIONBIO, \$true
			or die "cannot set child stdout to non-blocking: $!";
		ioctl $child_stderr, FIONBIO, \$true
			or die "cannot set child stderr to non-blocking: $!";
	} else {
		my $stdout_flags = fcntl $child_stdout, F_GETFL, 0
			or die "cannot get descriptor status flags of child stdout: $!\n";
		fcntl $child_stdout, F_GETFL, $stdout_flags | O_NONBLOCK
			or die "cannot set child stdout to non-blocking: $!\n";
		my $stderr_flags = fcntl $child_stderr, F_GETFL, 0
			or die "cannot get descriptor status flags of child stderr: $!\n";
		fcntl $child_stderr, F_GETFL, $stderr_flags | O_NONBLOCK
			or die "cannot set child stderr to non-blocking: $!\n";
	}

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
	debug "At least one child has output";

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

# FIXME! Make this callable with an array!
sub spawn_child_process {
	my ($perl, $script) = @_;

	if ('MSWin32' eq $^O) {
		return spawn_child_process_msdos($perl, $script);
	}

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

sub spawn_child_process_msdos {
	my ($perl, $script) = @_;

	# These two socketpairs for standard output and standard error
	# respectively will be polled by means of select.
	socketpair my $stdout_read, my $stdout_write,
			AF_UNIX, SOCK_STREAM, PF_UNSPEC
		or die "cannot create socketpair: $!\n";
	socketpair my $stderr_read, my $stderr_write,
			AF_UNIX, SOCK_STREAM, PF_UNSPEC
		or die "cannot create socketpair: $!\n";

	# Save standard output and standard error.
	open SAVED_OUT, '>&STDOUT' or die "cannot dup STDOUT: $!\n";
	open SAVED_ERR, '>&STDERR' or die "cannot dup STDERR: $!\n";
	
	# Redirect them to the write end of the socket pairs.
	open STDOUT, '>&' . $stdout_write->fileno
		or die "cannot redirect STDOUT to pipe: $!\n";
	open STDERR, '>&' . $stderr_write->fileno
		or die "cannot redirect STDERR to pipe: $!\n";

	# At this point, we can no longer write errors to STDERR because it is
	# the pipe.  We put everything into an eval block, and unconditionally
	# restore the file handles after it.
	my $child_pid;
	eval {
		require Win32::Process;

		my $process;
		Win32::Process::Create(
			$process,
			$perl, # Absolute path to image or search $PATH.
			"perl child.pl", # Must be escaped!
			0, # Inherit handles.
			0, # Creation flags.
			'.', # Working directory.
		) or die "cannot exec: ", Win32::FormatMessage(Win32::GetLastError());

		$child_pid = $process->GetProcessID;
	};
	my $x = $@; # Save that.

	# First restore STDERR;
	if (!open STDERR, '>&SAVED_ERR') {
		print SAVED_ERR "cannot restore STDERR: $!\n";
		exit 1;
	}

	# If there was an exception, re-throw it.  If you want to catch this
	# exception keep in mind that standard output will be in closed state.
	die $x if $x;

	# Now clean up the rest.
	open STDOUT, '>&SAVED_OUT' or die "cannot restore STDOUT: $!\n";

	return $child_pid, $stdout_read, $stderr_read;
}

sub debug {
	if (DEBUG) {
		my ($msg) = @_;

		chomp $msg;
		warn "[parent][debug]: $msg\n";
	}
}
