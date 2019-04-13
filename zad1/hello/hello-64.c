#include <stdio.h>
#include "crossld.h"

static int print(char *data) {
	printf("%s\n", data);
    return 77;
}

int main() {
	int res;
	enum type print_types[] = {TYPE_INT};
	struct function funcs[] = {
		{"print", print_types, 1, TYPE_VOID, print},
	};
	
	 res = crossld_start("hello-32", funcs, 1);
	 printf("Result: %d\n", res);
	 return res;
}
