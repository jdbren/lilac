.text
.globl acpica_int_handler
.extern AcpiInt
acpica_int_handler:
    pusha
    cld
    call AcpiInt
    popa
    iret