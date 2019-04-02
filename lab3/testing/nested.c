#include <stdio.h>
#include <string.h>

typedef void (*formatter) (int);

formatter make_formatter (const char *format) {
    char f[20];
    strcpy(f, format);
    void form(int x) {
        printf(f, x);
    }
    return *form;
}


int main() {
    formatter x08_format = make_formatter ("%08x\n");
    formatter xalt_format = make_formatter ("%#x\n");
    formatter d_format = make_formatter ("%d\n");
    formatter verbose_format = make_formatter ("Liczba: %9d!\n");

    verbose_format (0xdef0);
    x08_format (0x1234);
    xalt_format (0x5678);
    d_format (0x9abc);
    return 0;
}
