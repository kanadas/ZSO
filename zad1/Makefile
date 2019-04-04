all: crossld hello_world
clean:
	rm -f hello-32 hello-64 *.so *.o

crossld: crossld.c crossld.h run32.s
	gcc -fno-pic -no-pie -g -Wall -Wextra -std=c11 crossld.c run32.s -o crossld

hello_world: hello_world.c
	gcc -m32 -fno-pic -no-pie -static hello_world.c -o hello_world
