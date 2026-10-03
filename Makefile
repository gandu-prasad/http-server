CC=gcc

server: server.c
	${CC} $< -o $@

.PHONY: clean

clean:
	rm -f server
