.intel_syntax noprefix

.global jump_usermode
.type jump_usermode, @function
jump_usermode:
	push ebp
	mov ebp, esp
	push ebx
	push esi
	push edi
	mov ebx, [ebp+8] # get the return address
	mov edx, [ebp+12] # get the stack address
	mov ax, (4 * 8) | 3 # ring 3 data with bottom 2 bits set for ring 3
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax # SS is handled by iret

	# set up the stack frame iret expects
	push (4 * 8) | 3 	# data selector
	push edx 			# current esp
	pushf 			# eflags
	push (3 * 8) | 3 	# code selector (ring 3 code with bottom 2 bits set for ring 3)
	push ebx			# instruction address to return to
	iretd

user:
    1: jmp 1b

target_read:
    .long 0xdeadbeaf
    .long 0xdeadbeaf