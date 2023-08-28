CC = cc

DEFAULT = all

all: child

child: child.c
	$(CC) -o $@ $<