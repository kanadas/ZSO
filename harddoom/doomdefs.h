
//Register offsets
#define CMD_FREE 0x70
#define CMD_SEND 0x40 //8 fields (256 bits)
#define CMD_PT 0x60
#define CMD_SIZE 0x64
#define CMD_READ_IDX 0x68
#define CMD_WRITE_IDX 0x6c
#define FENCE_COUNTER 0x10
#define FENCE_WAIT 0x14
#define ENABLE 0x0
#define RESET 0x4
#define INTR 0x8
#define INTR_ENABLE 0xc
#define FE_CODE_ADDR 0x100
#define FE_CODE_WINDOW 0x104

//Useful constants
//flags for RESET register
#define RESET_TLB (1<<11)
#define RESET_CACHE (1<<12 | 1<<13 | 1<<14)
#define RESET_DEVICE 0xff7f7ffc

#define INTR_CLEAR 0xfff7
#define START_DEVICE 0x3ff
#define ENABLE_FETCH 0x1 //fetch block enable flag
