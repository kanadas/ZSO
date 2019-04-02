global _start

STDOUT equ 1
SYS_WRITE equ 1
SYS_CLONE equ 56
SYS_EXIT equ 60
SYS_EXIT_GROUP equ 231
SYS_WAIT4 equ 61
CLONE_VM equ 0x00000100

STACK_SIZE equ 1024

section .rodata
    A: db "A",10
    B: db "B",10

section .bss
    childStack resb STACK_SIZE
    wstatus resb 4

section .text
_start:
    mov rax, SYS_CLONE
    mov rdi, CLONE_VM
    mov rsi, childStack + STACK_SIZE
    xor rdx, rdx
    xor r10, r10
    push father
    mov QWORD [childStack + STACK_SIZE], child
    syscall ;CLONE
    ;TODO errors
    pop rbx
    jmp rbx

father:
    mov r15, rax ;PID
    mov rdi, STDOUT
    mov rsi, B
    mov rdx, 2
    xor rbx, rbx
loop_father:
    mov rax, SYS_WRITE
    syscall ;WRITE
    ;TODO errors
    add rbx, 1
    cmp rbx, 1000
    jl loop_father
    mov rax, SYS_WAIT4
    mov rdi, r15 ;PID
    mov rsi, wstatus
    xor rdx, rdx
    xor r10, r10
    syscall ;WAITPID
    ;TODO errors
    mov rax, SYS_EXIT_GROUP
    xor rdx, rdx
    syscall ;EXIT_GROUP

child:
    mov rdi, STDOUT
    mov rsi, A
    mov rdx, 2
    xor rbx, rbx
loop_child:
    mov rax, SYS_WRITE
    syscall ;WRITE
    ;TODO errors
    add rbx, 1
    cmp rbx, 1000
    jl loop_child
    mov rax, SYS_EXIT
    xor rdx, rdx
    syscall ;EXIT

