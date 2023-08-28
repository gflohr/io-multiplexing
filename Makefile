CC = cc
CFLAGS = -Wall
COMPILE = $(CC) $(CFLAGS)

DEFAULT = all

all: parent.exe child.exe

parent.exe: parent.c
	$(COMPILE) -o $@ $<

child.exe: child.c
	$(COMPILE) -o $@ $<