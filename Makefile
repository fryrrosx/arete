# Arete build file. GNU make. Recipes use '>' instead of tab (see .RECIPEPREFIX).
.RECIPEPREFIX := >

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -g
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)
BIN     := arete

.PHONY: all build test examples clean

all: build

build: $(BIN)

$(BIN): $(OBJ)
> $(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
> $(CC) $(CFLAGS) -c -o $@ $<

test: build
> sh tests/run.sh tests

examples: build
> sh tests/run.sh examples

clean:
> rm -f $(OBJ) $(BIN)
