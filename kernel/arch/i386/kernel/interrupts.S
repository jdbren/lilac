.intel_syntax noprefix

.macro isr_err name
    .extern \name\()_handler
    .global \name
    \name:
    pop eax
    pushad
    cld
    push eax
	call \name\()_handler
    add  esp, 4
    popad
    iretd
.endm

.macro isr_no_err name
    .extern \name\()_handler
    .global \name
    \name:
    pushad
    cld
	call \name\()_handler
    popad
    iretd
.endm

isr_no_err div0
# isr_no_err debug
# isr_no_err nmi
# isr_no_err brkp
# isr_no_err ovfl
# isr_no_err bound
isr_no_err invldop
# isr_no_err dna
isr_err    dblflt
# isr_no_err cso
# isr_err    invldtss
# isr_err    segnp
# isr_err    ssflt
isr_err    gpflt
isr_err    pgflt
# isr_no_err res
# isr_no_err flpexc
# isr_err    align
# isr_no_err mchk
# isr_no_err simd

# isr_no_err 20
# isr_no_err 21
# isr_no_err 22
# isr_no_err 23
# isr_no_err 24
# isr_no_err 25
# isr_no_err 26
# isr_no_err 27
# isr_no_err 28
# isr_no_err 29
# isr_err    30
# isr_no_err 31


.extern keyboard_interrupt
.global keyboard_handler
keyboard_handler:
    pushad
    cld
    call keyboard_interrupt
    popad
    iretd
