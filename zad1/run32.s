.global run32
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
    
    //It won't return - this code is for recycling
    pushl $0x33
    pushl $back64
    lret
    
.code64
run32:
    mov %rsp, %r11
    movq %rsi, %rsp
    subq $8, %rsp
    movq %r11, 8(%rsp)
    movl $0x23, 4(%rsp)
    movq $switch32, %rax
    movl %eax, (%rsp)
    lret

back64:
    pop %rsp
    ret
