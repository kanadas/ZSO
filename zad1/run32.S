
.text
.code64
begin:
run32:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    call act_pos
act_pos:
    pop %rbx
    lea act_pos - begin, %rax
    sub %rax, %rbx
    movq old_stack -  begin(%rbx), %r11
    movq %rsp, (%r11)
    movq %rsi, %rsp
    subq $4, %rsp
    movl $0x23, 4(%rsp)
    leaq switch32 - begin(%rbx), %rax
    movl %eax, (%rsp)
    lret

return32:
    call act_pos2
act_pos2:
    pop %rbx
    lea act_pos2 - begin, %rax
    sub %rax, %rbx
    movq old_stack - begin(%rbx), %r11
    movq (%r11), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    mov %edi, %eax
    ret

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

old_stack: .quad 0

