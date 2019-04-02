.text:
    sub rsp, 8
    mov esi, edi
    mov rdi, strict qword 0
    nop
    nop
    nop
    nop
    nop
    nop
    mov rax, strict qword 0
    call rax
    add rsp, 8
    ret
