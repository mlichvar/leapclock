CFLAGS=-O2 -Wall
LDFLAGS=-lrt -lncurses

all: leapclock

clean:
	rm -f leapclock
