.intel_syntax noprefix

.global disk_read
.type disk_read, @function
disk_read:
    push ebp
    mov ebp, esp
    push edi
    mov eax, [ebp+8]
    mov ecx, [ebp+12]
    mov edi, [ebp+16]
    call ata_lba_read
    pop edi
    pop ebp
    ret

.global disk_write
.type disk_write, @function
disk_write:
    push ebp
    mov ebp, esp
    push edi
    mov eax, [ebp+8]
    mov ecx, [ebp+12]
    mov edi, [ebp+16]
    call ata_lba_write
    pop edi
    pop ebp
    ret

# =============================================================================
#  ATA read sectors (LBA mode) 
# 
#  @param EAX Logical Block Address of sector
#  @param CL  Number of sectors to read
#  @param RDI The address of buffer to put data obtained from disk
# 
#  @return None
# =============================================================================
ata_lba_read:
    pushf
    and eax, 0x0FFFFFFF
    push ebx

    cmp cx, 256          #  Maximum number of sectors to read is 256
    jg .error            #  If CX > 256, error
    jl .ok
    xor ecx, ecx         #  If CX = 0, read 256 sectors

.ok: 
    mov ebx, eax         #  Save LBA in RBX

    mov edx, 0x01F6      #  Port to send drive and bit 24 - 27 of LBA
    shr eax, 24          #  Get bit 24 - 27 in al
    or al, 0xE0          #  Set bit 6 in al for LBA mode
    out dx, al

    mov dx, 0x01F2       #  Port to send number of sectors
    mov al, cl           #  Get number of sectors from CL
    out dx, al

    inc dx               #  Port to send bit 0 - 7 of LBA
    mov eax, ebx         #  Get LBA from EBX
    out dx, al

    inc dx               #  Port to send bit 8 - 15 of LBA
    mov eax, ebx         #  Get LBA from EBX
    shr eax, 8           #  Get bit 8 - 15 in AL
    out dx, al


    inc dx               #  Port to send bit 16 - 23 of LBA
    mov eax, ebx         #  Get LBA from EBX
    shr eax, 16          #  Get bit 16 - 23 in AL
    out dx, al

    mov dx, 0x1F7        #  Command port
    mov al, 0x20         #  Read with retry.
    out dx, al

    mov bx, cx
    cmp bl, 0            #  If BL = 0, read 256 sectors
    jne .still_going
    mov bx, 256

.still_going:  
    in al, dx
    test al, 8           #  the sector buffer requires servicing.
    jz .still_going      #  until the sector buffer is ready.

    mov cx, 256          #  RCX is counter for INSW, 256 words = 1 sector
    mov dx, 0x1F0        #  Data port, in and out
    rep insw             #  in to [RDI]
    dec bx      
    mov dx, 0x1F7        #  Command port
    jnz .still_going     #  read next sector

.error:
    pop ebx
    popf
    ret


# =============================================================================
#  ATA write sectors (LBA mode) 
# 
#  @param EAX Logical Block Address of sector
#  @param CL  Number of sectors to write
#  @param RDI The address of data to write to the disk
# 
#  @return None
# =============================================================================
 
ata_lba_write:
    pushf
    and eax, 0x0FFFFFFF
    push eax
    push ebx
    push ecx
    push edx
    push edi
 
    mov ebx, eax         #  Save LBA in RBX
 
    mov edx, 0x01F6      #  Port to send drive and bit 24 - 27 of LBA
    shr eax, 24          #  Get bit 24 - 27 in al
    or al, 0xE0          #  Set bit 6 in al for LBA mode
    out dx, al
 
    mov edx, 0x01F2      #  Port to send number of sectors
    mov al, cl           #  Get number of sectors from CL
    out dx, al
 
    mov edx, 0x1F3       #  Port to send bit 0 - 7 of LBA
    mov eax, ebx         #  Get LBA from EBX
    out dx, al
 
    mov edx, 0x1F4       #  Port to send bit 8 - 15 of LBA
    mov eax, ebx         #  Get LBA from EBX
    shr eax, 8           #  Get bit 8 - 15 in AL
    out dx, al
 
 
    mov edx, 0x1F5       #  Port to send bit 16 - 23 of LBA
    mov eax, ebx         #  Get LBA from EBX
    shr eax, 16          #  Get bit 16 - 23 in AL
    out dx, al
 
    mov edx, 0x1F7       #  Command port
    mov al, 0x30         #  Write with retry.
    out dx, al
 
.still_going_w:  in al, dx
    test al, 8           #  the sector buffer requires servicing.
    jz .still_going_w    #  until the sector buffer is ready.
 
    mov eax, 256         #  to read 256 words = 1 sector
    xor bx, bx
    mov bl, cl           #  write CL sectors
    mul bx
    mov ecx, eax         #  RCX is counter for OUTSW
    mov edx, 0x1F0       #  Data port, in and out
    mov esi, edi
    rep outsw            #  out
 
    pop edi
    pop edx
    pop ecx
    pop ebx
    pop eax
    popf
    ret
