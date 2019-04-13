asm (
	".global _start\n"
	"_start:\n"
	"call hello\n"
	"hlt\n"
);


_Noreturn void exit(int status);
int print(char *str);

void hello()
{
	int ret = print("Hello world");
	exit(ret);
}
