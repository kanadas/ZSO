.global run32
.global return32
.type run32, @function

.code32
switch32:
    pushl $0x2b
    popl %ds
    pushl $0x2b
    popl %es

    //setup arguments
    //program name
    leal 8(%esp), %eax
    pushl %eax
    //argc
    pushl $0
    pushl %edi
    ret

.code64
run32:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %r11
    movq %rsi, %rsp
    subq $8, %rsp
    movq %r11, 8(%rsp)
    movl $0x23, 4(%rsp)
    movq $switch32, %rax
    movl %eax, (%rsp)
    lret

return32:
    #TODO remember old stack pointer - this do not work
    pop %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    mov %edi, %eax
    ret
