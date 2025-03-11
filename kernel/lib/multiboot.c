#include <lilac/libc.h>
#include <lilac/boot.h>

void parse_multiboot(uintptr_t addr, struct multiboot_info *mbd)
{
    if (addr & 7)
        kerror("Unaligned mbi:\n");

    struct multiboot_tag *tag;
    for (tag = (struct multiboot_tag *)(addr + 8);
        tag->type != MULTIBOOT_TAG_TYPE_END;
        tag = (struct multiboot_tag *) ((multiboot_uint8_t *) tag
            + ((tag->size + 7) & ~7)))
    {
        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_CMDLINE:
                mbd->cmdline = ((struct multiboot_tag_string*)tag)->string;
            break;
            case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
                mbd->bootloader = ((struct multiboot_tag_string*)tag)->string;
            break;
            case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
                mbd->meminfo = (struct multiboot_tag_basic_meminfo*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_BOOTDEV:
                mbd->boot_dev = (struct multiboot_tag_bootdev*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_MMAP:
                mbd->mmap = (struct multiboot_tag_mmap*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_FRAMEBUFFER:
                mbd->framebuffer = (struct multiboot_tag_framebuffer*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_EFI32:
                mbd->efi32 = (struct multiboot_tag_efi32*)tag;
            break;
            // case MULTIBOOT_TAG_TYPE_ACPI_OLD:
            // 	mbd->old_acpi = (struct multiboot_tag_old_acpi*)tag;
            // break;
            case MULTIBOOT_TAG_TYPE_ACPI_NEW:
                mbd->new_acpi = (struct multiboot_tag_new_acpi*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_EFI_MMAP:
                mbd->efi_mmap = (struct multiboot_tag_efi_mmap*)tag;
            break;
            case MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR:
                mbd->base_addr = (struct multiboot_tag_load_base_addr*)tag;
            break;
            default:
                printf("Unknown multiboot tag: %d\n", tag->type);
            break;
        }
    }
    printf("Kernel loaded at: 0x%x\n", mbd->base_addr->load_base_addr);
}
