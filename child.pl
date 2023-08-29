#! /usr/bin/env perl

# ABSTRACT: Platform-independent example for IO multiplexing

use strict;

use Time::HiRes qw(usleep);

autoflush STDOUT, 1;
autoflush STDERR, 1;

my $us;

while (1) {
	$us = int rand 3_000_000;
	print STDOUT "Perl child pid $$ writing to stdout before sleeping $us μs.\n";
	usleep $us;
	$us = int rand 3_000_000;
	print STDERR "Perl child pid $$ writing to stderr before sleeping $us μs.\n";
	usleep $us;
}