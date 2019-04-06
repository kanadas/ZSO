all: crossld hello_world trampoline.bin
clean:
	rm -f hello-32 hello-64 *.so *.o

crossld: crossld.c crossld.h run32.s
	gcc -g -fno-pic -no-pie -Wall -Wextra -std=c11 crossld.c run32.s -o crossld

hello_world: hello_world.c write.s
	gcc -g -m32 -fno-pic -no-pie hello_world.c write.s -o hello_world

trampoline.bin: trampoline.s
	as -o trampoline.o trampoline.s
	ld --oformat binary -o trampoline.s trampoline.o
	#objcopy -O binary trampoline.elf trampoline.bin
