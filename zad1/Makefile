all: crossld
clean:
	rm -f hello-32 hello-64 *.so *.o

crossld: crossld.c crossld.h run32.S
	gcc -g -fno-pic -no-pie -Wall -Wextra -std=c11 crossld.c run32.S -o crossld

hello_world: hello_world.c write.S
	gcc -g -m32 -fno-pic -no-pie hello_world.c write.S -o hello_world

trampoline.bin: trampoline.S
	as -o trampoline.o trampoline.S
	ld --oformat binary -o trampoline.bin trampoline.o
	#objcopy -O binary trampoline.elf trampoline.bin
