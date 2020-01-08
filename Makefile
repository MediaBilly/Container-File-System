CC = gcc
FLAGS = -Wall
SOURCES = $(wildcard ./src/*.c)
EXECUTABLES = $(SOURCES:./src/%.c=%)

all:$(EXECUTABLES)

%:./src/%.c
	$(CC) $(FLAGS) -o $@ $<

.PHONY : clean

clean:
	rm -f $(EXECUTABLES)
