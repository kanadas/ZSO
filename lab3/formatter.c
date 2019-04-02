#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>

typedef void (*formatter) (int);
typedef void (*printf_t) (char*, int);
//typedef int (*int_f) (int);

void print_int(char *format, int param) {
    printf(format, param);
}

formatter make_formatter(const char *format) {
    char *form = (char*) malloc(strlen(format) + 1);
    if(form == NULL) {
        printf("Błąd podczas alokacji pamięci\n");
        exit(1);
    }
    strcpy(form, format);
    int fd = open("tramp.bin", O_RDONLY);
    if(fd == -1) {
        free(form);
        printf("Błąd podczas czytania pliku");
        exit(1);
    }
    void* f = mmap(NULL, 40, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
    close(fd);
    //int* ftab = (int*)f;
    //printf("%x %x %x %x", ftab[0], ftab[1], ftab[2], ftab[3]);
    ((char**)f)[1] = form;
    ((printf_t*)f)[3] = *print_int;
    return (formatter)f;
}

int main() {
    formatter x08_format = make_formatter ("%08x\n");
    formatter xalt_format = make_formatter ("%#x\n");
    formatter d_format = make_formatter ("%d\n");
    formatter verbose_format = make_formatter ("Liczba: %9d!\n");

    x08_format (0x1234);
    xalt_format (0x5678);
    d_format (0x9abc);
    verbose_format (0xdef0);

    //int square_fd = open("square.o", O_RDONLY);
    //int_f square = (int_f) mmap(NULL, 17, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, square_fd, 0);
    //int x = square(5);
    //printf("%d\n", x);
    return 0;
}
