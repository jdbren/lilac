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
invalid_opcode:
    hlt

.global double_fault
double_fault:
    hlt


.extern keyboard_interrupt
.global keyboard_handler
keyboard_handler:
    pushad
    cld
    call keyboard_interrupt
    popad
    iretd
