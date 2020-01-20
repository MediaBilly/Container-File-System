CC = gcc
FLAGS = -Wall
TARGETS = src/main.o src/cfs.o src/string_functions.o src/minheap.o src/queue.o

cfs:$(TARGETS)
	$(CC) $(FLAGS) -o cfs $(TARGETS)

src/main.o:src/main.c
	$(CC) $(FLAGS) -o src/main.o -c src/main.c

src/cfs.o:src/cfs.c
	$(CC) $(FLAGS) -o src/cfs.o -c src/cfs.c

src/string_functions.o:src/string_functions.c
	$(CC) $(FLAGS) -o src/string_functions.o -c src/string_functions.c

src/minheap.o:src/minheap.c
	$(CC) $(FLAGS) -o src/minheap.o -c src/minheap.c

src/queue.o:src/queue.c
	$(CC) $(FLAGS) -o src/queue.o -c src/queue.c

.PHONY : clean

clean:
	rm -f $(TARGETS)