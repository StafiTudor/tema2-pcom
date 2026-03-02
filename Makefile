CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lm

all: server subscriber

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

subscriber: subscriber.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c

.PHONY: all clean

clean:
	rm -f server subscriber
