#include <string.h>
#include <kernel/panic.h>
#include <utility/acpi.h>
#include <paging.h>
#include <io.h>

void log(char *msg) {
    for (int i = 0; msg[i] != 0; i++)
        outb(0xe9, msg[i]);
}

void parse_madt(struct SDTHeader *addr) {
    u8 check = 0;
    for (int i = 0; i < addr->Length; i++)
        check += ((char *)addr)[i];
    if((u8)(check) != 0)
        printf("Checksum is incorrect\n");
    if (memcmp(addr->Signature, "APIC", 4))
        kerror("MADT signature incorrect\n");

    struct MADT *madt = (struct MADT*)addr;

    printf("MADT: %x\n", madt);
    printf("Local APIC Address: %x\n", madt->LocalApicAddress);
    printf("Flags: %x\n", madt->Flags);

    struct MADTEntry *entry = (struct MADTEntry*)(madt + 1);
    for (; (u32)entry < (u32)madt + madt->header.Length; entry = (struct MADTEntry*)((u32)entry + entry->Length)) {
        switch (entry->Type) {
            // case 0:
            //     printf("Processor Local APIC\n");
            //     printf("APIC ID: %d", ((u8*)entry)[3]);
            //     printf("Flags: %x\n", ((u32*)entry)[1]);
            //     break;
            case 1:
                printf("I/O APIC\n");
                printf("I/O APIC ID: %d\n", ((u8*)entry)[2]);
                printf("I/O APIC Address: %x\n", ((u32*)entry)[1]);
                printf("Global System Interrupt Base: %d\n", ((u32*)entry)[2]);
                break;
            case 2:
                //printf("Interrupt Source Override\n");
                printf("Bus Source: %d\n", ((u8*)entry)[3]);
                printf("IRQ Source: %d\n", ((u8*)entry)[4]);
                printf("Global System Interrupt: %d\n", ((u32*)entry)[1]);
                printf("Flags: %x\n", ((u32*)entry)[2]);
                break;
            // case 3:
            //     printf("I/O Non-maskable Interrupt Source\n");
            //     printf("Global System Interrupt: %d\n", ((u32*)entry)[1]);
            //     printf("Flags: %x\n", ((u32*)entry)[2]);
            //     break;
            // case 4:
            //     printf("APIC Non-maskable Interrupt Source\n");
            //     printf("APIC ID: %x\n", ((u8*)entry)[2]);
            //     printf("Flags: %x\n", *(u16*)((u8*)entry+3));
            //     printf("Local APIC LINT: %x\n", ((u8*)entry)[5]);
            //     break;
            // default:
            //     printf("Unknown\n");
        }
    }


}

void read_rsdt(struct SDTHeader *rsdt) {
    u8 check = 0;
    for (int i = 0; i < rsdt->Length; i++)
        check += ((char *)rsdt)[i];
    if((u8)(check) != 0)
        printf("Checksum is incorrect\n");
    if (memcmp(rsdt->Signature, "RSDT", 4))
        kerror("RSDT signature incorrect\n");

    int entries = (rsdt->Length - sizeof(*rsdt)) / 4;

    u32 *other_entries = (u32*)((u32)rsdt + sizeof(*rsdt));
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)other_entries[i];
        if(!memcmp(h->Signature, "APIC", 4))
            parse_madt(h);
    }

}

void read_rsdp(struct RSDP *rsdp) {
    u8 check = 0;
    for (int i = 0; i < sizeof(*rsdp); i++)
        check += ((char *)rsdp)[i];
    if((u8)(check) != 0)
        printf("Checksum is incorrect\n");

    map_page((void*)(rsdp->RsdtAddress & 0xfffff000), 
            (void*)(rsdp->RsdtAddress & 0xfffff000), 0x1);

    read_rsdt((struct SDTHeader*)rsdp->RsdtAddress);
}
