#define CR0_TS 3
#define CR0_EM 2

.text
.globl _fpu_init
_fpu_init:
    movl %cr0, %edx                 # Start probe, get CR0
    andl $(~(CR0_TS + CR0_EM)), %edx # clear TS and EM to force fpu access
    movl %edx, %cr0                 # store control word
    FNINIT                          # load defaults to FPU
    FNSTSW (.testword)              # store status word
    cmpw $0, (.testword)            # compare the written status with the expected FPU state
    jne .nofpu                      # jump if the FPU hasn't written anything (i.e. it's not there)
    jmp .hasfpu

.nofpu:
    movl $0, %eax
    ret

.hasfpu:
    movl $1, %eax
    FLDCW (.value_37A)
    ret

.data
.testword:
    .word 0x55AA
.value_37A:
    .word 0x37A          # control word for the FPU
