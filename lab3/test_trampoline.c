#include "trampoline.c"

void* cur_addr() {
    return __builtin_return_address(0);
}

int main() {
    void* addr = cur_addr();
    printf("Current addr: %p\n", addr);
    printf("Format: %p\n", format);
    printf("print_int: %p\n", print_int);
}
