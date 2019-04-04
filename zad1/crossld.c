#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <elf.h>
#include "crossld.h"

#define STACK_SIZE 40960

int64_t PAGE_SIZE;

#define PAGESTART(_v) ((_v) & ~(PAGE_SIZE - 1))
#define PAGEOFFSET(_v) ((_v) & (PAGE_SIZE - 1))

extern void run32(void* code, void* stack);

static void err(const char *err)
{
    fprintf(stderr, "%s: %s\n", err, strerror(errno));
    exit(1);
}

static void free_hdr_err(const char *err_msg, Elf32_Phdr *p_hdr, Elf32_Shdr *s_hdr) {
    if(p_hdr != NULL) free(p_hdr);
    if(s_hdr != NULL) free(s_hdr);
    err(err_msg);
}

//returns entry point pointer
void* readelf(const char *fname) {
    int fd;
    Elf32_Ehdr e_hdr;
    Elf32_Phdr *p_hdr = NULL;
    Elf32_Shdr *s_hdr = NULL;

    //elf header validation
    if((fd = open(fname, O_RDONLY)) == -1) 
        err("open file");
    if(read(fd, &e_hdr, sizeof(e_hdr)) != sizeof(e_hdr)) 
        err("read header");
    if(memcmp(e_hdr.e_ident, ELFMAG, SELFMAG)) 
        err("not an elf - wrong magic");
    if(e_hdr.e_ident[EI_CLASS] != ELFCLASS32) 
        err("not 32-bit binary");
    if(e_hdr.e_type != ET_EXEC) 
        err("not ET_EXEC");
    if(e_hdr.e_machine != EM_386) 
        err("not i386 binary");
    
    //loading elf program headers
    //for loader
    uint32_t p_hdr_s = e_hdr.e_phentsize * e_hdr.e_phnum;
    if((p_hdr = malloc(p_hdr_s)) == NULL) err("malloc"); 
    if(pread(fd, p_hdr, p_hdr_s, e_hdr.e_phoff) == -1) 
        free_hdr_err("read program section", p_hdr, s_hdr);
    short loaded = 0;
    for(int i = 0; i < e_hdr.e_phnum; ++i) 
        if(p_hdr[i].p_type == PT_LOAD) {
            loaded = 1;
            //TODO fix leaks
            int flags = 0;
            if(p_hdr[i].p_flags & PF_X) flags |= PROT_EXEC;
            if(p_hdr[i].p_flags & PF_W) flags |= PROT_WRITE;
            if(p_hdr[i].p_flags & PF_R) flags |= PROT_READ;
            mmap((void*)PAGESTART(p_hdr[i].p_vaddr), 
                p_hdr[i].p_filesz + PAGEOFFSET(p_hdr[i].p_vaddr),
                flags,
                MAP_FIXED | MAP_PRIVATE,
                fd,
                p_hdr[i].p_offset - PAGEOFFSET(p_hdr[i].p_vaddr));
            //BSS
            int bss_s = p_hdr[i].p_memsz - p_hdr[i].p_filesz;
            if(bss_s > 0) {
                uint32_t bss = p_hdr[i].p_vaddr + p_hdr[i].p_filesz;
                int rest = PAGE_SIZE - PAGEOFFSET(bss);
                uint32_t bss_b = PAGE_SIZE + PAGESTART(bss);
                if(rest < PAGE_SIZE) {
                    //Not sure this is needed
                    memset((void*)bss, 0, rest);
                } else {
                    rest = 0;
                    bss_b = bss;
                } if(rest < bss_s)
                    mmap((void*)bss_b, bss_s - rest, flags, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
        }
    if(!loaded) return NULL;
    //loading elf section headers
    //for linker
    //if((s_hdr = malloc(e_hdr.e_shentsize * e_hdr.e_shnum)) == NULL)
    //    free_hdr_err("malloc", p_hdr, s_hdr);
    return (void*)e_hdr.e_entry;
}

int crossld_start(const char *fname, const struct function *funcs, int nfuncs) {
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    int fname_len = strlen(fname);
    if(fname_len > PATH_MAX) err("file doesn't exist - too long file path");
    void(*entry)(void) = readelf(fname);
    //Happy (one-way) jump
    if(entry != NULL) {
        void* stack;
        if((stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0)) == MAP_FAILED)
            err("stack map");
        //stack = (void*) (((uint64_t) stack) + 4096 - 4);
        
        //pass program name - 0 argument
        strcpy(stack + STACK_SIZE - 8 - fname_len, fname);
        run32(entry, stack + STACK_SIZE - fname_len - 16);
    }
    return 0;
}

int main() {
    crossld_start("hello_world", NULL, 0);
    return 0;
}
