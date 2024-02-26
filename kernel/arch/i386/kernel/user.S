.intel_syntax noprefix

.global jump_usermode
.type jump_usermode, @function
jump_usermode:
	push ebp
	mov ebp, esp
	push ebx
	push esi
	push edi
	mov ebx, [ebp+8] 	# get the return address
	mov edx, [ebp+12] 	# get the stack address
	mov eax, 0x20 | 0x3 # ring 3 data
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax # SS is handled by iret

	# set up iret stack frame
	push eax 			# data selector
	push edx 			# current esp
	pushf
	mov eax, [esp]
	or eax, 0x200
	mov [esp], eax
	push 0x18 | 0x3 	# code selector (ring 3)
	push ebx			# instruction address to return to
	iretd

user:
    1: jmp 1b

target_read:
    .long 0xdeadbeaf
    .long 0xdeadbeaf