global _start

STDOUT equ 1
SYS_READ equ 0
SYS_WRITE equ 1
SYS_OPEN equ 2
SYS_CLOSE equ 3
SYS_EXIT equ 60
ENDL equ 10

section .data
	msg db "Czesc!",10,0
	endl db ENDL
	
section .bss
	buf resb 256
	bufptr resq 1

%macro exit 1
	mov eax, SYS_EXIT
	mov edi, %1
	syscall
%endmacro

%macro print 2 ;prints string, second argument is length
	mov edx, %2
	mov rsi, %1
	mov eax, SYS_WRITE
	mov edi, STDOUT
	syscall
%endmacro

%macro print_num 1 ;prints number
	mov rax, %1
	mov ebp, 1
	mov BYTE [buf + 255], 10
	mov QWORD [bufptr], buf + 255
%%print_num_loop:
	xor rdx, rdx
	mov rcx, 10
	div rcx
	add dl, '0'
	dec QWORD [bufptr]
	inc ebp
	mov rbx, [bufptr]
	mov [rbx], dl
	cmp rax, 0
	jne %%print_num_loop	
	print [bufptr], ebp
%endmacro

%macro print_nt 1 ;prints null terminated string
	mov rdi, %1
	xor al, al
	xor ecx, ecx
	not ecx
	cld
	repne scasb
	not ecx
	dec ecx
	print %1, ecx	
%endmacro	

section .text

_start:
	;print_nt msg
	;print_num 74
	pop rax
	push rax
	print_num rax
	pop rax
print_args_loop:
	pop rbx
	push rax
	print_nt rbx
	print endl, 1
	pop rax
	dec rax
	cmp rax, 0
	jne print_args_loop
exit_program:	
	exit 1	
	
