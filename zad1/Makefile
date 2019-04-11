all: crossld
clean:
	rm -f *.so *.o

crossld: crossld.c crossld.h run32.S trampoline.S
	gcc -g -Wl,-soname=libcrossld.so -shared -fPIC -Wall -Wextra crossld.c run32.S -o libcrossld.so

trampoline.bin: trampoline.S
	as -o trampoline.o trampoline.S
	ld --oformat binary -o trampoline.bin trampoline.o
	#objcopy -O binary trampoline.elf trampoline.bin
