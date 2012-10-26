particles:
	gcc -std=c99 -O3 particles.c -lSDL -lGL -o particles
glinfo: glinfo.c
	gcc -std=c99 glinfo.c -lSDL -lGL -o glinfo

clean:
	rm -f glinfo particles