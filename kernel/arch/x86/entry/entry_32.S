// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
.intel_syntax noprefix
#include <asm/segments.h>

.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	push ebp
	mov ebp, esp
	push ebx
	push esi
	push edi
	push esp
	call set_tss_esp0
	add esp, 4
	mov ebx, [ebp+8] 			# return address
	mov edx, [ebp+12] 			# stack address
	mov eax, __USER_DS | 0x3 	# ring 3 data
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	# set up iret stack frame
	push eax 			 # ss
	push edx 			 # esp
	pushf
	mov eax, [esp]
	or eax, 0x200
	mov [esp], eax		 # eflags
	push __USER_CS | 0x3 # cs (ring 3)
	push ebx			 # eip
	iretd
