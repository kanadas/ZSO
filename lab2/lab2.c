#define _GNU_SOURCE
#include <sys/wait.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

int child(void* arg) 
{
    (void)arg;
    for(int i = 0; i < 1000; ++i) {
        write(STDOUT_FILENO, "A\n", 2);
    }
    _exit(0);
}

const int STACK_SIZE = 10240;

int main() 
{
    char *stack, *stackTop;
    stack = (char*)malloc(STACK_SIZE);
    stackTop = stack + STACK_SIZE;
    if(stack == NULL) return 1;
    pid_t pid = clone(child, stackTop, CLONE_VM, NULL);
    if(pid == -1) return 1;
    for(int i = 0; i < 1000; ++i) {
        write(STDOUT_FILENO, "B\n", 2);
    }
    int wstatus;
    pid_t w = waitpid(pid, &wstatus, 0);
    int retcode = 0;
    if(w == -1) retcode = 1;
    syscall(SYS_exit_group, retcode);
}

