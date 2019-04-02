all: crossld hello_world
clean:
	rm -f hello-32 hello-64 *.so *.o

crossld: crossld.c crossld.h
	gcc -O3 -Wall -Wextra -std=c11 crossld.c -o crossld

hello_world: hello_world.c
	gcc -m32 -fno-pic -no-pie hello_world.c -o hello_world
