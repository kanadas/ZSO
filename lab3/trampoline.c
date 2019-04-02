#include <stdio.h>

void print_int(char *format, int param) {
    printf(format, param);
}

char format[6] = "%08x\n";

void trampoline(int param) {
    print_int(format, param);
}

