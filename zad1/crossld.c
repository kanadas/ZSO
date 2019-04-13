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

//make trampoline.bin
#define TRAMPOLINE_SIZE 301
#define ARG_SIZE 29
#define PARAM_T_OFFSET TRAMPOLINE_SIZE - 29
#define ARG_CNT_OFFSET TRAMPOLINE_SIZE - 21
#define FUN_ADDR_OFFSET TRAMPOLINE_SIZE - 17
#define RET_T_OFFSET TRAMPOLINE_SIZE - 9
#define EXIT_ADDR_OFFSET TRAMPOLINE_SIZE - 8
unsigned char trampoline[TRAMPOLINE_SIZE] = "\
\x55\x53\x56\x57\x89\xE5\x6A\x33\xE8\x00\x00\x00\x00\x5B\x8D\x05\x0D\x00\x00\x00\
\x29\xC3\x8D\x83\x2B\x00\x00\x00\x50\xCB\x6A\x2B\x1F\x6A\x2B\x07\x89\xEC\x5F\x5E\
\x5B\x5D\xC3\x67\x48\x8B\x83\x10\x01\x00\x00\x49\x89\xC7\x67\x44\x8B\x93\x18\x01\
\x00\x00\x49\xFF\xC2\x4C\x8D\x5D\x14\xE8\x94\x00\x00\x00\x48\x89\xC7\xE8\x8C\x00\
\x00\x00\x48\x89\xC6\xE8\x84\x00\x00\x00\x48\x89\xC2\xE8\x7C\x00\x00\x00\x48\x89\
\xC1\xE8\x74\x00\x00\x00\x49\x89\xC0\xE8\x6C\x00\x00\x00\x49\x89\xC1\x4D\x89\xD4\
\x49\xC1\xE4\x03\x4C\x29\xE4\x48\x83\xE4\xF0\x49\x89\xE4\xE8\x53\x00\x00\x00\x49\
\x89\x04\x24\x49\x83\xC4\x08\xEB\xF1\x67\x48\x8B\x83\x1C\x01\x00\x00\xFF\xD0\x48\
\x89\xC2\x48\xC1\xEA\x20\x67\x8A\x8B\x24\x01\x00\x00\x84\xC9\x75\x17\x48\x83\xFA\
\x00\x74\x11\x48\xC7\xC7\xFF\xFF\xFF\xFF\x67\x48\x8B\x83\x25\x01\x00\x00\xFF\xD0\
\x48\x83\xEC\x08\xC7\x44\x24\x04\x23\x00\x00\x00\x67\x4C\x8D\x5B\x1E\x44\x89\x1C\
\x24\xCB\x49\xFF\xCA\x75\x04\x41\x5E\xEB\xAE\x41\xF6\x07\x01\x75\x18\x41\xF6\x07\
\x02\x75\x09\x49\x63\x03\x49\x83\xC3\x04\xEB\x10\x41\x8B\x03\x49\x83\xC3\x04\xEB\
\x07\x49\x8B\x03\x49\x83\xC3\x08\x49\xFF\xC7\xC3\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00";

//make run32.bin
#define RUN32_SIZE 126
#define RETURN32_OFFSET 0x3F
#define OLD_STACK_PTR_OFFSET RUN32_SIZE - 8
unsigned char run32_code[RUN32_SIZE] = "\
\x53\x55\x41\x54\x41\x55\x41\x56\x41\x57\xE8\x00\x00\x00\x00\x5B\x48\x8D\x04\x25\
\x0F\x00\x00\x00\x48\x29\xC3\x4C\x8B\x9B\x76\x00\x00\x00\x49\x89\x23\x48\x89\xF4\
\x48\x83\xEC\x04\xC7\x44\x24\x04\x23\x00\x00\x00\x48\x8D\x83\x67\x00\x00\x00\x89\
\x04\x24\xCB\xE8\x00\x00\x00\x00\x5B\x48\x8D\x04\x25\x44\x00\x00\x00\x48\x29\xC3\
\x4C\x8B\x9B\x76\x00\x00\x00\x49\x8B\x23\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x5D\x5B\
\x89\xF8\xC3\x6A\x2B\x1F\x6A\x2B\x07\x8D\x44\x24\x08\x50\x6A\x00\x57\xC3\x00\x00\
\x00\x00\x00\x00\x00\x00";

void* run32;
void* return32;

#define STACK_SIZE 4096000

int64_t PAGE_SIZE;

#define PAGESTART(_v) ((_v) & ~(PAGE_SIZE - 1))
#define PAGEOFFSET(_v) ((_v) & (PAGE_SIZE - 1))

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

typedef struct {
    int64_t code_ptr;
    const char *name;
} rel_fun;

rel_fun *tramps = NULL;
int ntramps = 0;

static int free_tramps() {
    if(tramps == NULL) return 0;
    int ret = 0, temp;
    for(int i = 0; i < ntramps; ++i) {
        free(*((int8_t**)(tramps[i].code_ptr + PARAM_T_OFFSET)));
        temp = munmap((void*)(tramps[i].code_ptr), TRAMPOLINE_SIZE);
        ret = MIN(ret, temp);
    }
    free(tramps);
    tramps = NULL;
    ntramps = 0;
    return ret;
}

typedef struct {
    void* ptr;
    ssize_t len;
} mapping;

mapping *mappings = NULL;
ssize_t mappings_len = 0;
uint32_t mappings_num = 0;

static int free_mappings() {
    if(mappings == NULL) return 0;
    int ret = 0, temp;
    for(uint32_t i = 0; i < mappings_num; ++i) {
        temp = munmap(mappings[i].ptr, mappings[i].len);
        ret = MIN(ret, temp);
    }
    free(mappings);
    mappings = NULL;
    mappings_len = 0;
    mappings_num = 0;
    return ret;
}

int elf_fd = -1;

static int free_mem()
{
    int res = 0, temp;
    if(elf_fd != -1) res = close(elf_fd);
    elf_fd = -1;
    temp = free_tramps();
    res = MIN(res, temp);
    temp = free_mappings();
    res = MIN(res, temp);
    return res;
}

static int add_mapping(void* ptr, ssize_t len) {
    if(mappings_num >= mappings_len) {
        mapping *n_map;
        mappings_len *= 2;
        if(mappings_len == 0) mappings_len = 8;
        if((n_map = realloc(mappings, mappings_len * sizeof(mapping))) == NULL) {
            free_mem();
            return -1;
        }
        mappings = n_map;
    }
    mappings[mappings_num].ptr = ptr;
    mappings[mappings_num++].len = len;
    return 0;
}

Elf32_Ehdr e_hdr;
Elf32_Phdr *p_hdr = NULL;

//returns entry point pointer
void* readelf(const char *fname) {
    //elf header validation
    if((elf_fd = open(fname, O_RDONLY)) == -1) 
        return NULL;
    if(read(elf_fd, &e_hdr, sizeof(e_hdr)) != sizeof(e_hdr)) {
        errno = ENOEXEC;
        free_mem();
        return NULL;
    }
    if(memcmp(e_hdr.e_ident, ELFMAG, SELFMAG) != 0) {
        errno = ENOEXEC;
        free_mem();
        return NULL;
    }
    if(e_hdr.e_ident[EI_CLASS] != ELFCLASS32) {
        errno = ENOEXEC;
        free_mem();
        return NULL;
    }
    if(e_hdr.e_type != ET_EXEC) {
        errno = ENOEXEC;
        free_mem();
        return NULL;
    }
    if(e_hdr.e_machine != EM_386) {
        errno = ENOEXEC;
        free_mem();
        return NULL;
    }
    
    //loading elf program headers
    uint32_t p_hdr_s = e_hdr.e_phentsize * e_hdr.e_phnum;
    if((p_hdr = malloc(p_hdr_s)) == NULL) {
        free_mem(); 
        return NULL;
    }
    if(pread(elf_fd, p_hdr, p_hdr_s, e_hdr.e_phoff) == -1) {
        free(p_hdr);
        free_mem();
        return NULL;
    }
    short loaded = 0;
    void *map_start, *map_ptr;
    ssize_t map_size;
    int map_offset;
    for(int i = 0; i < e_hdr.e_phnum; ++i) {
        if(p_hdr[i].p_type == PT_LOAD) {
            loaded = 1;
            int flags = 0;
            if(p_hdr[i].p_flags & PF_X) flags |= PROT_EXEC;
            if(p_hdr[i].p_flags & PF_W) flags |= PROT_WRITE;
            if(p_hdr[i].p_flags & PF_R) flags |= PROT_READ;
            map_start = (void*)PAGESTART(p_hdr[i].p_vaddr);
            map_size = p_hdr[i].p_filesz + PAGEOFFSET(p_hdr[i].p_vaddr);
            map_offset = p_hdr[i].p_offset - PAGEOFFSET(p_hdr[i].p_vaddr);
            if((map_ptr = mmap(map_start, map_size, flags, MAP_PRIVATE, elf_fd, map_offset)) != map_start) {
                if(map_ptr != NULL) {
                    munmap(map_ptr, map_size);
                    errno = EFAULT;
                }
                free(p_hdr);
                free_mem();
                return NULL;
            }
            if(add_mapping(map_start, map_size) == -1) return NULL;
            //BSS
            int bss_s = p_hdr[i].p_memsz - p_hdr[i].p_filesz;
            if(bss_s > 0) {
                void* bss = (void*)(uint64_t)p_hdr[i].p_vaddr + p_hdr[i].p_filesz;
                int rest = PAGE_SIZE - PAGEOFFSET((uint64_t)bss);
                void* bss_b = (void*)PAGE_SIZE + PAGESTART((uint64_t)bss);
                if(rest < PAGE_SIZE) {
                    //Not sure this is needed
                    memset(bss, 0, rest);
                } else {
                    rest = 0;
                    bss_b = bss;
                } if(rest < bss_s) {
                    if((map_ptr = mmap(bss_b, bss_s - rest, flags, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) != bss_b) {
                        if(map_ptr != NULL) {
                            munmap(map_ptr, bss_s - rest);
                            errno = EFAULT;
                        }
                        free(p_hdr);
                        free_mem();
                        return NULL;
                    }
                    if(add_mapping(bss_b, bss_s - rest) == -1) return NULL;
                }
            }
        } 
    }    

    if(!loaded) {
        errno = ENOMEDIUM; //Nothing executable found
        return NULL;
    } 
    return (void*)(uint64_t)e_hdr.e_entry;

}

int link_elf() {
    
    Elf32_Rel *rel_ptr = NULL;
    Elf32_Sym *sym_ptr = NULL;
    Elf32_Dyn *dyn = NULL;
    char *str_ptr = NULL;
    ssize_t rel_s = -1;

    for(int i = 0; i < e_hdr.e_phnum; ++i) {
        if(p_hdr[i].p_type == PT_DYNAMIC) {
            if((dyn = malloc(p_hdr[i].p_memsz)) == NULL) {
                free(p_hdr);
                free_mem();
                return -1;
            }
            if(pread(elf_fd, dyn, p_hdr[i].p_memsz, p_hdr[i].p_offset) == -1) {
                free(dyn);
                free(p_hdr);
                free_mem();
                return -1;
            }
            for(int i = 0; dyn[i].d_tag != DT_NULL; ++i) {
                switch (dyn[i].d_tag) {
                    case DT_JMPREL:     //.rel.plt address
                        rel_ptr = (Elf32_Rel*)(uint64_t)dyn[i].d_un.d_ptr;
                        break;
                    case DT_PLTRELSZ:   //.rel.plt size
                        rel_s = dyn[i].d_un.d_val / sizeof(Elf32_Rel);
                        break;
                    case DT_SYMTAB:     //.dynsym address
                        sym_ptr = (Elf32_Sym*)(uint64_t)dyn[i].d_un.d_ptr;
                        break;
                    case DT_STRTAB:     //.dynstr address
                        str_ptr = (char*)(uint64_t)dyn[i].d_un.d_ptr;
                        break;
                }
            }
            free(dyn);
        }
    }

    if(close(elf_fd) < 0) {
        elf_fd = -1;
        free_mem();
        return -1;
    }
    elf_fd = -1;

    int undefined;

    if(rel_ptr != 0 && rel_s > 0) {
        Elf32_Sym sym;
        for(int i = 0; i < rel_s; ++i) {
            undefined = 1;
            sym = sym_ptr[ELF32_R_SYM(rel_ptr[i].r_info)];
            for(int j = 0; j < ntramps; ++j)
                if(strcmp(tramps[j].name, str_ptr + sym.st_name) == 0) {
                    memcpy((void*)(uint64_t)rel_ptr[i].r_offset, &tramps[j].code_ptr, 4);
                    undefined = 0;
                    break;
                }
            if(undefined) {
                free_mem();
                return -1;
            }
        }
    }
    free(p_hdr);
    return 1;
}

int32_t create_trampoline(const struct function *fun) {
    void *f;
    if((f = mmap(NULL, TRAMPOLINE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_32BIT | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        free_mem();
        return -1;
    }
    memcpy(f, trampoline, TRAMPOLINE_SIZE);
    uint8_t *types;
    if((types = malloc(fun->nargs)) == NULL) {
        munmap(f, TRAMPOLINE_SIZE);
        free_mem();
        return -1;
    }
    for(int i = 0; i < fun->nargs; ++i) {
        types[i] = 0; //32bit signed
        if(fun->args[i] == TYPE_LONG_LONG)
            types[i] = 1; //64bit signed
        else if(fun->args[i] == TYPE_UNSIGNED_INT || fun->args[i] == TYPE_UNSIGNED_LONG || fun->args[i] == TYPE_PTR) 
            types[i] = 2; //32bit unsigned
        else if(fun->args[i] == TYPE_UNSIGNED_LONG_LONG)
            types[i] = 3; //64bit unsigned
    }
    uint8_t return_t = 1;
    if(fun->result == TYPE_PTR || fun->result == TYPE_LONG || fun->result == TYPE_UNSIGNED_LONG) {
        return_t = 0;
    }
    void* exit_addr = return32;
    memcpy(f + PARAM_T_OFFSET, &types, 8);
    memcpy(f + ARG_CNT_OFFSET, &(fun->nargs), 4);
    memcpy(f + FUN_ADDR_OFFSET, &(fun->code), 8);
    memcpy(f + RET_T_OFFSET, &return_t, 1);
    memcpy(f + EXIT_ADDR_OFFSET, &exit_addr, 8);
    if(mprotect(f, TRAMPOLINE_SIZE, PROT_READ | PROT_EXEC) == -1) {
        free(types);
        munmap(f, TRAMPOLINE_SIZE);
        free_mem();
        return -1;
    }
    return (uint64_t)f; //MAP_32BIT guarantees not negative address (first 2GB)
}

int crossld_start(const char *fname, const struct function *funcs, int nfuncs) {
    PAGE_SIZE = sysconf(_SC_PAGESIZE);

    int fname_len = strlen(fname);
    if(fname_len > PATH_MAX) {
        errno = EINVAL; //No such file - too long name
        return -1;
    }

    void(*entry)(void) = readelf(fname);
    if(entry == NULL) {
        free_mem();
        return -1;
    }
    
    if((run32 = mmap(NULL, RUN32_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_32BIT | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        free_mem();
        return -1;
    }
    add_mapping(run32, RUN32_SIZE);
    return32 = run32 + RETURN32_OFFSET;
    memcpy(run32, run32_code, RUN32_SIZE);
    
    //create trampolines
    tramps = (rel_fun*)malloc(sizeof(rel_fun)*(nfuncs + 1));
    for(ntramps = 0; ntramps < nfuncs; ++ntramps) {
        if((tramps[ntramps].code_ptr = create_trampoline(funcs + ntramps)) < 0) return -1;
        tramps[ntramps].name = funcs[ntramps].name;
    }
    
    //create exit trampoline
    enum type exit_types[] = {TYPE_INT};
    struct function exit_funcs[] = {
        {"exit", exit_types, 1, TYPE_VOID, return32},
    };
    if((tramps[ntramps].code_ptr = create_trampoline(exit_funcs)) < 0) return -1;
    tramps[ntramps++].name = "exit";

    if(link_elf() == -1) {
        free_mem();
        return -1;
    }

    void* stack;
    if((stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0)) == MAP_FAILED) {
        free_mem();
        return -1;
    }
    add_mapping(stack, STACK_SIZE);
    //pass program name - 0 argument
    strcpy(stack + STACK_SIZE - 8 - fname_len, fname);
    
    //Space to save old stack
    void* old_stack = malloc(8);
    if(old_stack == NULL) {
        free_mem();
        return -1;
    }
    memcpy(run32 + OLD_STACK_PTR_OFFSET, &old_stack, 8);
    if(mprotect(run32, RUN32_SIZE, PROT_READ | PROT_EXEC) == -1) {
        free(old_stack);
        free_mem();
        return -1;
    }

    int ret = ((int(*)(void*,void*))run32)(entry, stack + STACK_SIZE - fname_len - 16);

    free(old_stack);
    if(free_mem() < 0) return -1;
    return ret;
}

