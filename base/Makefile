GCC_FLAGS = -std=c99

base: base.c common.o math.o
	gcc $(GCC_FLAGS) base.c common.o math.o -lSDL -lGL -o base

common.o: common.c common.h
	gcc -c $(GCC_FLAGS) common.c

math.o: math.c math.h
	gcc -c $(GCC_FLAGS) math.c

clean:
	rm -f *.o base core