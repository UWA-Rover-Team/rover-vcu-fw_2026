CC = gcc
CFLAGS = -Wall -Wextra -O2

canudp_daemon: canudp_daemon.c canudp_proto.h
	$(CC) $(CFLAGS) -o $@ canudp_daemon.c

clean:
	rm -f canudp_daemon
