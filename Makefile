CC = gcc
SOURCES = client/client.c server/server.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = all

$(TARGET): server/server client/client

server/server: server/server.o
	$(CC) -o $@ $^

client/client: client/client.o
	$(CC) -o $@ $^

.PHONY: clean fclean

clean:
	@rm -f server/server client/client $(OBJECTS)

fclean: clean
	@rm -f client/files/received/*
	@rm -f server/files/*