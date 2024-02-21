.intel_syntax noprefix

.bss
.align 32
system_timer_fractions:  .skip 32   # Fractions of 1 ms since timer initialized
system_timer_ms:         .skip 32   # Number of whole ms since timer initialized
IRQ0_fractions:          .skip 32   # Fractions of 1 ms between IRQs
IRQ0_ms:                 .skip 32   # Number of whole ms between IRQs
IRQ0_frequency:          .skip 32   # Actual frequency of PIT
PIT_reload_value:        .skip 16   # Current PIT reload value

.text
.global timer_handler
timer_handler:
	push eax
	push ebx

	mov eax, [IRQ0_fractions]
	mov ebx, [IRQ0_ms]                    # eax.ebx = amount of time between IRQs
	add [system_timer_fractions], eax     # Update system timer tick fractions
	adc [system_timer_ms], ebx            # Update system timer tick milli-seconds

    mov eax, [sched_timer]
    cmp eax, -1
    je .sched_done
    test eax, eax
    jz run_scheduler
    dec eax
    mov [sched_timer], eax
.sched_done:
    mov eax, [timer_cnt]
    test eax, eax
    jz TimerDone
    dec eax
    mov [timer_cnt], eax

TimerDone:
	xor eax, eax
    mov ebx, [lapic_base]
    mov [ebx+0x0b0], eax                  # Send the EOI to the APIC

	pop ebx
	pop eax
	iretd

run_scheduler:
    xor eax, eax
    mov ebx, [lapic_base]
    mov [ebx+0x0b0], eax
    pushad
    pushfd
    call sched_tick
    popfd
    popad
    jmp .sched_done

# Input
# param 1: desired frequency in Hz
.global init_PIT
.type init_PIT, @function
init_PIT:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]                  # ebx = desired frequency in Hz

    #  Do some checking
    mov eax, 0x10000                   # eax = reload value for slowest possible frequency (65536)
    cmp ebx, 18                        # Is the requested frequency too low?
    jbe .gotReloadValue                #  yes, use slowest possible frequency

    mov eax, 1                         # ax = reload value for fastest possible frequency (1)
    cmp ebx, 1193181                   # Is the requested frequency too high?
    jae .gotReloadValue                #  yes, use fastest possible frequency

    #  Calculate the reload value
    mov eax, 3579545
    mov edx, 0                         # edx:eax = 3579545
    div ebx                            # eax = 3579545 / frequency, edx = remainder
    cmp edx, 3579545 / 2               # Is the remainder more than half?
    jb .l1                             #  no, round down
    inc eax                            #  yes, round up
.l1:
    mov ebx, 3
    mov edx, 0                        # edx:eax = 3579545 * 256 / frequency
    div ebx                           # eax = (3579545 * 256 / 3 * 256) / frequency
    cmp edx, 3 / 2                    # Is the remainder more than half?
    jb .l2                            #  no, round down
    inc eax                           #  yes, round up
.l2:

#  Store the reload value and calculate the actual frequency
.gotReloadValue:
    push eax                          # Store reload_value for later
    mov [PIT_reload_value],ax         # Store the reload value for later
    mov ebx, eax                      # ebx = reload value

    mov eax, 3579545
    mov edx, 0                        # edx:eax = 3579545
    div ebx                           # eax = 3579545 / reload_value, edx = remainder
    cmp edx, 3579545 / 2              # Is the remainder more than half?
    jb .l3                            #  no, round down
    inc eax                           #  yes, round up
.l3:
    mov ebx, 3
    mov edx, 0                        # edx:eax = 3579545 / reload_value
    div ebx                           # eax = (3579545 / 3) / frequency
    cmp edx, 3 / 2                    # Is the remainder more than half?
    jb .l4                            #  no, round down
    inc eax                           #  yes, round up
.l4:
    mov [IRQ0_frequency],eax          # Store the actual frequency for displaying later

    #  Calculate the amount of time between IRQs in 32.32 fixed point
    #
    #  Note: The basic formula is:
    #   time in ms = reload_value / (3579545 / 3) * 1000
    #  This can be rearranged in the following way:
    #   time in ms = reload_value * 3000 / 3579545
    #   time in ms = reload_value * 3000 / 3579545 * (2^42)/(2^42)
    #   time in ms = reload_value * 3000 * (2^42) / 3579545 / (2^42)
    #   time in ms * 2^32 = reload_value * 3000 * (2^42) / 3579545 / (2^42) * (2^32)
    #   time in ms * 2^32 = reload_value * 3000 * (2^42) / 3579545 / (2^10)

    pop ebx                           # ebx = reload_value
    mov eax, 0xDBB3A062               # eax = 3000 * (2^42) / 3579545
    mul ebx                           # edx:eax = reload_value * 3000 * (2^42) / 3579545
    shrd eax, edx, 10
    shr edx, 10                       # edx:eax = reload_value * 3000 * (2^42) / 3579545 / (2^10)

    mov [IRQ0_ms], edx                # Set whole ms between IRQs
    mov [IRQ0_fractions], eax         # Set fractions of 1 ms between IRQs


    # Program the PIT channel
    pushfd

    mov al, 0x34                       # channel 0, lobyte/hibyte, rate generator
    out 0x43, al

    mov ax, [PIT_reload_value]         # ax = 16 bit reload value
    out 0x40, al                       # Set low byte of PIT reload value
    mov al, ah                         # ax = high 8 bits of reload value
    out 0x40, al                       # Set high byte of PIT reload value

    popfd

    pop ebx
    pop ebp
    ret
