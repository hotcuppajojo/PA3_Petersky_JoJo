CC=gcc
CFLAGS=-Wall -pthread

sched: sched.c
	$(CC) $(CFLAGS) -o sched sched.c

.PHONY: clean
clean:
	rm -f *.o sched
