all: crossld
clean:
	rm -f *.so *.o

crossld: crossld.c crossld.h run32.S trampoline.S
	gcc -Wl,-soname=libcrossld.so -shared -fPIC -Wall -Wextra crossld.c -o libcrossld.so

trampoline.bin: trampoline.S
	as -o trampoline.o trampoline.S
	ld --oformat binary -o trampoline.bin trampoline.o
	#objcopy -O binary trampoline.elf trampoline.bin

run32.bin: run32.S
	as -o run32.o run32.S
	ld --oformat binary -o run32.bin run32.o

