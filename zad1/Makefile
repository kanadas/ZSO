all: crossld
clean:
	rm -f *.so *.o

crossld: crossld.c crossld.h run32.S trampoline.S
	#as -g -o trampoline.o trampoline.S
	#as -g -o run32.o run32.S
	gcc -g -c -Wl,-soname=libcrossld.so -shared -fPIC -Wall -Wextra crossld.c trampoline.S run32.S
	gcc -Wl,-soname=libcrossld.so -fPIC -shared crossld.o run32.o trampoline.o -o libcrossld.so

#trampoline.bin: trampoline.S
#	as -o trampoline.o trampoline.S
#	ld --oformat binary -o trampoline.bin trampoline.o
	#objcopy -O binary trampoline.elf trampoline.bin
