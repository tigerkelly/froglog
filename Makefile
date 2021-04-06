
# This make file has only been tested on a RPI 3B+.
CC=gcc

SRC=froglog.c

LDFLAGS=-g -L/usr/local/lib -L../utils/libs -lini -lstrutils -lz -lpthread -lpq -lm
CFLAGS=-std=gnu99

CFLAGS += -g -Wall -O2 -I./ -I../utils/incs -I/usr/include/postgresql

PRG=froglog

all: $(PRG)

$(PRG): $(PRG).o
	$(CC) $(PRG).o -o $(PRG) $(LDFLAGS)
	cp $(PRG) ~/bin/$(PRG)

$(PRG).o: $(PRG).c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(PRG).o $(PRG)

