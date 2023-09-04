# platform-independent-ipc

Example for platform-independent, asynchronous IPC with child processes.

## Additional Information

This repository illustrates the information of the blog post at
http://www.guido-flohr.net/platform-independent-asynchronous-child-process-ipc.

Source code is provided for Perl and C.

## Building

The source code provided here is not meant to be installed but you should
build and run it locally.

### Perl

The repository does not have any other dependencies than Perl itself.  Open a
terminal and run:

```sh
$ perl parent.pl
Running forever, hit CTRL-C to stop.
[child 69554][info]: Perl child pid 69554 writing to stdout before sleeping 2261943 μs.
[child 69555][info]: Perl child pid 69555 writing to stdout before sleeping 858209 μs.
[child 69556][info]: Perl child pid 69556 writing to stdout before sleeping 2370161 μs.
[child 69555][error]: Perl child pid 69555 writing to stderr before sleeping 2860666 μs.
...
```

This will launch the script `parent.pl` which in turn starts the script
`child.pl` three times.  Standard output and standard error of all child
processes is piped back into the parent process which logs it to its own
standard output, whenever a full line of output is read.

The prefix "[child `PID`][`SCOPE`]" is added as a log decoration by the parent
process.  Compare that to the output of running a child process standalone:

```sh
$ perl child.pl
Perl child pid 71879 writing to stdout before sleeping 460897 μs.
Perl child pid 71879 writing to stderr before sleeping 1832933 μs.
Perl child pid 71879 writing to stdout before sleeping 874834 μs.
Perl child pid 71879 writing to stderr before sleeping 2937516 μs.
...
```

The child processes write these lines to standard output and standard error
respectively, before sleeping a random period of time between 0 and 3 seconds.

You can set the environment variable `DEBUG_IO_MULTIPLEXING` to a truthy
value if you want to enable additional debugging information.

Hint: The file `dist.ini` is used for `dzil` (`Dist::Zilla`).  You can safely
ignore it, if you do not know what it is used for.

### C

The same example is provided in C.  Every C compiler out there should be good
enough to compile the example.  You also need a POSIX compliant standard C
library.

If you have `make` (on some BSD systems or MS-DOS aka Windows also try `gmake`),
you can just do:

```sh
$ make
```

This creates the two executables `parent.exe` and `child.exe`.  The extension
`.exe` is used on all systems for this example, not just on MS-DOS!

You can run these executables just like the Perl scripts described above.  The
output is almost identical.

For simplicity, the C version searches `child.exe` always in the current
working directory.

If your system lacks `make` but has a C compiler, you can also compile the
two executables manually:

```sh
$ cc parent.c -o parent.exe
$ cc child.c -o child.exe
```

Replace `cc` with the path to your C compiler if `cc` cannot be found.  Also
note that the order of the command-line arguments has to be exactly like this
if you are compiling with `gcc` on MS-DOS.

## See Also

* https://stackoverflow.com/questions/10569805/what-is-the-preferred-cross-platform-ipc-perl-module
