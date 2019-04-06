.global write
.type write, @function

#it does not work for some reason
#.data
#    hello: .string "Hello World!"

.code32:
write:
    mov     $4, %eax    # sys_write call number 
    mov     $1, %ebx    # write to stdout (fd=1)
    mov     hello, %ecx
    mov     $13, %edx
    int     $0x80   
    ret
