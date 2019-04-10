.text
begin:
param_t: .zero 8    #pointer to byte array of argument types
arg_cnt: .long -1   #argument count
fun_addr: .zero 8   #function address
ret_t: .byte -1     #return type (0 => 32 bits (or void), otherwise 64 bits)

.code32
_start:
    push   %ebp
    mov    %esp, %ebp
    push   %ebx
    push   %esi
    push   %edi
    pushl  $0x33
    call   act_pos
act_pos:
    pop    %ebx
    lea    act_pos - begin, %eax 
    sub    %eax, %ebx               #address of begin of file in ebx
    lea    switch64 - begin(%ebx), %eax
    pushl  %eax
    lret   

back32:
    pushl  $0x2b
    popl   %ds
    pushl  $0x2b
    popl   %es
    pop    %edi
    pop    %esi
    pop    %ebx
    mov    %ebp, %esp
    pop    %ebp

.code64
switch64:
    movq   param_t - begin(%ebx), %rax
    mov    %rax, %r15
    movl   arg_cnt - begin(%ebx), %r10d
    inc    %r10
    lea    8(%rbp), %r11
   
    call   extract_arg
    mov    %rax, %rdi
    call   extract_arg
    mov    %rax, %rsi
	call   extract_arg
    mov    %rax, %rdx
    call   extract_arg
    mov    %rax, %rcx
    call   extract_arg
    mov    %rax, %r8
    call   extract_arg
    mov    %rax, %r9

    mov    %r10, %r12
    shl    $3, %r12
    sub    %r12, %rsp   #alloc memory for agruments on stack
    and    $-15, %rsp   #align stack
    mov    %rsp, %r12
arg_loop:
    call   extract_arg
    mov    %rax, (%r12)
    add    $8, %r12
    jmp    arg_loop
arg_end:
    movq   fun_addr - begin(%ebx), %rax
    callq  *%rax
    movb   ret_t - begin(%ebx), %cl   #ret_t = 0 => 32bit (or void), otherwise 64 bit
    test   %cl, %cl
    je     finish
    mov    %rax, %rdx
    shr    $32, %rdx    #pass upper half of rax in edx
finish:
    sub    0x4, %rsp
    movl   $0x23, 0x4(%rsp)
    lea    back32 - begin(%ebx), %r11
    mov    %r11d, (%rsp)
    lret   

/* Extracts argument (from stack) pointed by r11 to rax assuming:
 * r10 is number remaining arguments + 1
 * r15 points to byte array of argument types such: 
 *   1st bit => 32 / 64 bit size, 2nd bit => signed / unsigned
 * and updates those registers*/
extract_arg:
    dec    %r10
    jne    extract_continue
    pop    %r14     #discard return address
    jmp    arg_end
extract_continue:
    testb  $0x1, (%r15)
    jne    extract_qarg
    testb  $0x2, (%r15)
    jne    extract_larg_uns
    movslq (%r11), %rax
    add    $0x4, %r11
    jmp    extract_fin
extract_larg_uns:
    mov    (%r11), %eax
    add    $0x4, %r11
    jmp    extract_fin
extract_qarg:
    mov    (%r11), %rax
    add    $0x8, %r11
extract_fin:
    inc    %r15
    ret

