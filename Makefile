# Arete build file. GNU make. Recipe lines start with a tab.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -g
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)
BIN     := arete

.PHONY: all build test examples clean

all: build

build: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

test: build
	sh tests/run.sh tests
	sh tests/cli.sh

examples: build
	sh tests/run.sh examples

clean:
	rm -f $(OBJ) $(BIN)
