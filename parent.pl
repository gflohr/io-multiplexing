#! /usr/bin/env perl

use strict;

use AnyEvent;
use AnyEvent::Loop;
use Time::HiRes;

sub spawn_child_process;

print "Running forever, hit CTRL-C to stop.\n";

my @info_handles;
my @error_handles;

my $script = __FILE__;
$script =~ s/parent\.pl$/child.pl/;

foreach (1 .. 3) {
	# The variable $^X contains the path to the Perl interpreter.
	my ($pid, $info_handle, $error_handle) = spawn_child_process $^X, $script;

	# Make sure that the file handles do not go out of scope and get reaped
	# by the garbage collector.
	push @info_handles, $info_handle;
	push @error_handles, $error_handle;
}

AnyEvent::Loop::run;

sub spawn_child_process {
	my ($perl, $script) = @_;

	pipe my $info_read, my $info_write or die "cannot create pipe: $!\n";
	pipe my $error_read, my $error_write or die "cannot create pipe: $!\n";

	my $pid = fork;
	if ($pid) {
		# Parent process.
		return $pid, $info_read, $error_read;
	} else {
		die "cannot fork: $!\n" if !defined $pid;
		exec $perl, $script or die "cannot exec $perl $script: $!\n";
	}
}
