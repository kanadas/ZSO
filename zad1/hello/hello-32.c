asm (
	".global _start\n"
	"_start:\n"
	"call hello\n"
	"hlt\n"
);


_Noreturn void exit(int status);
void print(char *str);

void hello()
{
	print("Hello world");
	exit(0);
}
