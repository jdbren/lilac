.intel_syntax noprefix

.extern div0_interrupt
.global div0
div0:
    pushad
    cld
    call div0_interrupt
    popad
    hlt

.global debug
debug:
    pushad
    cld
    
    popad
    iretd

.global invalid_opcode
.extern invalid_opcode_handler
invalid_opcode:
    hlt
    pushad
    cld
    call invalid_opcode_handler
    popad
    hlt

.global double_fault
double_fault:
    hlt

# need to implement
.extern pg_fault_handler
.globl page_fault
page_fault:
    pop eax
    pushad
    cld
    push eax
    call pg_fault_handler
    add  esp, 4
    popad
    iretd

.extern gp_fault_handler
.globl gp_fault
gp_fault:
    pop eax
    pushad
    cld
    push eax
    call gp_fault_handler
    add  esp, 4
    popad
    iretd


.extern keyboard_interrupt
.global keyboard_handler
keyboard_handler:
    pushad
    cld
    call keyboard_interrupt
    popad
    iretd
