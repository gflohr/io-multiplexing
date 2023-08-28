#! /usr/bin/env perl

# ABSTRACT: Platform-independent example for IO multiplexing

use strict;
use v5.10;

use Time::HiRes qw(usleep);

autoflush STDOUT, 1;
autoflush STDERR, 1;

my $us;

while (1) {
	$us = int rand 3_000_000;
	say STDOUT "child pid $$ writing to stdout before sleeping $us μs.";
	usleep $us;
	$us = int rand 3_000_000;
	say STDOUT "child pid $$ writing to stderr before sleeping $us μs.";
	usleep $us;
}