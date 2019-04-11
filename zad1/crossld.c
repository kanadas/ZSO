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

#define STACK_SIZE 4096000
#define TEMPLATE_SIZE 257
#define TEMPLATE_DATA 21

int64_t PAGE_SIZE;

#define PAGESTART(_v) ((_v) & ~(PAGE_SIZE - 1))
#define PAGEOFFSET(_v) ((_v) & (PAGE_SIZE - 1))

extern int run32(void* code, void* stack);
extern void return32(int* status);

int tramp_fd;

typedef struct {
    uint32_t code_ptr;
    const char *name;
} rel_fun;

rel_fun *tramps = NULL;
int ntramps = 0;

static void free_tramps() {
    if(tramps == NULL) return;
    for(int i = 0; i < ntramps; ++i) {
        free(((uint8_t**)(tramps[i].code_ptr - TEMPLATE_DATA))[0]);
        munmap((void*)(tramps[i].code_ptr - TEMPLATE_DATA), TEMPLATE_SIZE);
    }
    free(tramps);
}

typedef struct {
    void* ptr;
    ssize_t len;
} mapping;

mapping *mappings = NULL;
ssize_t mappings_len = 0;
uint32_t mappings_num = 0;

static void free_mappings() {
    if(mappings == NULL) return;
    for(uint32_t i = 0; i < mappings_num; ++i) {
        munmap(mappings[i].ptr, mappings[i].len);
    }
    free(mappings);
}

static void err(const char *err)
{
    fprintf(stderr, "%s: %d %s\n", err, errno, strerror(errno));
    exit(1);
}

static void free_err(const char *err_msg)
{
    free_tramps();
    free_mappings();
    err(err_msg);
}

static void close_free_err(const char *err_msg, int fd) {
    close(fd);
    free_err(err_msg);
}

static void add_mapping(void* ptr, ssize_t len) {
    if(mappings_num >= mappings_len) {
        mapping *n_map;
        mappings_len *= 2;
        if(mappings_len == 0) mappings_len = 8;
        if((n_map = realloc(mappings, mappings_len * sizeof(mapping))) == NULL)
            free_err("realloc mappings");
        mappings = n_map;
    }
    mappings[mappings_num].ptr = ptr;
    mappings[mappings_num++].len = len;
}

//returns entry point pointer
void* readelf(const char *fname) {
    int fd;
    Elf32_Ehdr e_hdr;
    Elf32_Phdr *p_hdr = NULL;
    //Elf32_Shdr *s_hdr = NULL;
    Elf32_Dyn *dyn = NULL;
    Elf32_Rel *rel_ptr = NULL;
    Elf32_Sym *sym_ptr = NULL;
    char *str_ptr = NULL;
    ssize_t rel_s = -1;//, sym_s = -1, str_s = -1;
    //elf header validation
    if((fd = open(fname, O_RDONLY)) == -1) 
        err("open file");
    if(read(fd, &e_hdr, sizeof(e_hdr)) != sizeof(e_hdr)) 
        close_free_err("read header", fd);
    if(memcmp(e_hdr.e_ident, ELFMAG, SELFMAG)) close_free_err("not an elf - wrong magic", fd);
    if(e_hdr.e_ident[EI_CLASS] != ELFCLASS32) 
        close_free_err("not 32-bit binary", fd);
    if(e_hdr.e_type != ET_EXEC) 
        close_free_err("not ET_EXEC", fd);
    if(e_hdr.e_machine != EM_386) 
        close_free_err("not i386 binary", fd);
    
    //loading elf program headers
    uint32_t p_hdr_s = e_hdr.e_phentsize * e_hdr.e_phnum;
    if((p_hdr = malloc(p_hdr_s)) == NULL) 
        close_free_err("malloc", fd); 
    if(pread(fd, p_hdr, p_hdr_s, e_hdr.e_phoff) == -1) {
        free(p_hdr);
        close_free_err("mmap program headers", fd);
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
            if((map_ptr = mmap(map_start, map_size, flags, MAP_PRIVATE, fd, map_offset)) != map_start) {
                if(map_ptr != NULL) munmap(map_ptr, map_size);
                free(p_hdr);
                close_free_err("mmap page" ,fd);
            }
            add_mapping(map_start, map_size);
            //BSS
            int bss_s = p_hdr[i].p_memsz - p_hdr[i].p_filesz;
            if(bss_s > 0) {
                void* bss = (void*) p_hdr[i].p_vaddr + p_hdr[i].p_filesz;
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
                        if(map_ptr != NULL) munmap(map_ptr, bss_s - rest);
                        free(p_hdr);
                        free_err("mmap bss");
                    }
                    add_mapping(bss_b, bss_s - rest);
                }
            }
        } else if(p_hdr[i].p_type == PT_DYNAMIC) {
            if((dyn = malloc(p_hdr[i].p_memsz)) == NULL) {
                free(p_hdr);
                free_err("malloc dynamic section");
            }
            if(pread(fd, dyn, p_hdr[i].p_memsz, p_hdr[i].p_offset) == -1) {
                free(dyn);
                free(p_hdr);
                close_free_err("pread dynamic section", fd);
            }
            for(int i = 0; dyn[i].d_tag != DT_NULL; ++i) {
                switch (dyn[i].d_tag) {
                    case DT_JMPREL:     //.rel.plt address
                        rel_ptr = (Elf32_Rel*)dyn[i].d_un.d_ptr;
                        break;
                    case DT_PLTRELSZ:   //.rel.plt size
                        rel_s = dyn[i].d_un.d_val / sizeof(Elf32_Rel);
                        break;
                    case DT_SYMTAB:     //.dynsym address
                        sym_ptr = (Elf32_Sym*)dyn[i].d_un.d_ptr;
                        break;
                    //case DT_SYMENT:     //.dynsym size
                    //    sym_s = dyn[i].d_un.d_val;
                    //    break;
                    case DT_STRTAB:     //.dynstr address
                        str_ptr = (char*)dyn[i].d_un.d_ptr;
                        break;
                    //case DT_STRSZ:      //.dynstr size
                    //    str_s = dyn[i].d_un.d_val;
                }
            }
            free(dyn);
        }
    }

    close(fd);

    if(!loaded) return NULL;
    if(rel_ptr != 0 && rel_s > 0) {
        Elf32_Sym sym;
        for(int i = 0; i < rel_s; ++i) {
           sym = sym_ptr[ELF32_R_SYM(rel_ptr[i].r_info)];
           for(int j = 0; j < ntramps; ++j)
               if(strcmp(tramps[j].name, str_ptr + sym.st_name) == 0) {
                   //TODO not sure it have to have write permissions (but should)
                   memcpy((void*)rel_ptr[i].r_offset, &tramps[j].code_ptr, 4);
               }
        }
    }
    free(p_hdr);
    return (void*)e_hdr.e_entry;
}

uint32_t create_trampoline(const struct function *fun) {
    void *f;
    if((f = mmap(NULL, TEMPLATE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_32BIT, tramp_fd, 0)) == MAP_FAILED) {
        close(tramp_fd);
        free_err("trampoline mmap");
    }
    uint8_t *types;
    if((types = malloc(fun->nargs)) == NULL) {
        close(tramp_fd);
        munmap(f, TEMPLATE_SIZE);
        free_err("trampoline malloc");
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
    uint8_t ret_t = 0;
    //TODO when TYPE_PTR || TYPE_(UNSIGNED)_LONG check length of return (32 bytes)
    if(fun->result == TYPE_LONG_LONG || fun->result == TYPE_UNSIGNED_LONG_LONG) {
        ret_t = 1;
    }
    memcpy(f, &types, 8);
    memcpy(f + 8, &(fun->nargs), 4);
    memcpy(f + 12, &(fun->code), 8);
    memcpy(f + 20, &ret_t, 1);
    if(mprotect(f, TEMPLATE_SIZE, PROT_READ | PROT_EXEC) == -1) {
        close(tramp_fd);
        free(types);
        munmap(f, TEMPLATE_SIZE);
        free_err("mprotect");
    }
    return (uint32_t)f + TEMPLATE_DATA;
}

int crossld_start(const char *fname, const struct function *funcs, int nfuncs) {
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    tramp_fd = open("trampoline.bin", O_RDONLY);
    
    //create trampolines
    tramps = (rel_fun*)malloc(sizeof(rel_fun)*nfuncs);
    for(ntramps = 0; ntramps < nfuncs; ++ntramps) {
        tramps[ntramps].code_ptr = create_trampoline(funcs + ntramps);
        tramps[ntramps].name = funcs[ntramps].name;
    }
    //create exit trampoline
    enum type exit_types[] = {TYPE_INT};
    struct function exit_funcs[] = {
        {"exit", exit_types, 1, TYPE_VOID, return32},
    };
    tramps[ntramps].code_ptr = create_trampoline(exit_funcs);
    tramps[ntramps++].name = "exit";

    close(tramp_fd);

    int fname_len = strlen(fname);
    if(fname_len > PATH_MAX) err("file doesn't exist - too long file path");
    void(*entry)(void) = readelf(fname);
    if(entry != NULL) {
        void* stack;
        if((stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0)) == MAP_FAILED)
            err("stack map");
        //pass program name - 0 argument
        strcpy(stack + STACK_SIZE - 8 - fname_len, fname);
        int ret = run32(entry, stack + STACK_SIZE - fname_len - 16);
        free_tramps();
        free_mappings();
        return ret;
    }
    return 0;
}

static void print(char *data) {
    printf("%s\n", data);
}

int main() { 
    enum type print_types[] = {TYPE_PTR};
    struct function funcs[] = {
        {"print", print_types, 1, TYPE_VOID, print},
    };
    int ret = crossld_start("hello/hello-32", funcs, 1);
    printf("Crossld returned: %d\n", ret);
    return 0;
}
