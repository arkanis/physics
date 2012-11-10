glinfo: glinfo.c
	gcc -std=c99 glinfo.c -lSDL -lGL -o glinfo

clean:
	rm -f glinfo