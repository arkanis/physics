GCC_FLAGS = -std=gnu99 -g

base: base.c common.o math.o viewport.o model.o
	gcc $(GCC_FLAGS) base.c common.o math.o viewport.o model.o -lSDL -lGL -o base

model.o: model.c model.h math.c math.h
	gcc -c $(GCC_FLAGS) model.c

common.o: common.c common.h
	gcc -c $(GCC_FLAGS) common.c

viewport.o: viewport.c viewport.h math.h
	gcc -c $(GCC_FLAGS) viewport.c

math.o: math.c math.h
	gcc -c $(GCC_FLAGS) math.c

clean:
	rm -f *.o base core