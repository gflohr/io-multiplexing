ifeq ($(OS),Windows_NT)
	CC = gcc
	LIBS = -lws2_32
else
	CC = cc
	LIBS =
endif

CFLAGS = -Wall

DEFAULT = all

all: parent.exe child.exe

parent.exe: parent.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

child.exe: child.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

selectable-socketpair.exe: selectable-socketpair.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
