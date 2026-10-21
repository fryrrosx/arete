# Arete build file. GNU make 3.81 or newer. Recipe lines start with a tab.

CC      ?= cc
CSTD    := -std=c11 -Wall -Wextra -Wpedantic
CFLAGS  ?= -g
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)
BIN     := arete

.PHONY: all build test examples clean

all: build

build: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CSTD) $(CFLAGS) -o $@ $^ -lm

# Vendored code is compiled as GNU C11 without our warning flags.
src/linenoise.o: src/linenoise.c src/linenoise.h
	$(CC) -std=gnu11 -O1 -w -c -o $@ $<

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CSTD) $(CFLAGS) -c -o $@ $<

test: build
	sh tests/run.sh tests
	sh tests/cli.sh

examples: build
	sh tests/run.sh examples

clean:
	rm -f $(OBJ) $(BIN)
