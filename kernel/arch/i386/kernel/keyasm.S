.intel_syntax noprefix
.extern keyboard_interrupt
.global keyboard_handler
keyboard_handler:
    pushad
    cld
    call keyboard_interrupt
    popad
    iretd