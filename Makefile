CC = gcc
FLAGS = -Wall
TARGETS = src/main.o src/cfs.o src/utilities.o

cfs:$(TARGETS)
	$(CC) $(FLAGS) -o cfs $(TARGETS)

src/main.o:src/main.c
	$(CC) $(FLAGS) -o src/main.o -c src/main.c

src/cfs.o:src/cfs.c
	$(CC) $(FLAGS) -o src/cfs.o -c src/cfs.c

src/utilities.o:src/utilities.c
	$(CC) $(FLAGS) -o src/utilities.o -c src/utilities.c

.PHONY : clean

clean:
	rm -f $(TARGETS)
