TARGETS=server

CFLAGS=-O0 -g -Wall -Wvla -Werror -Wno-error=unused-variable

all: $(TARGETS)

server: server.c
	gcc $(CFLAGS) -o server server.c

clean:
	rm -f $(TARGETS)
