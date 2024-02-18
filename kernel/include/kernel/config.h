#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#define PAGE_SIZE           4096
#define __KERNEL_BASE       0xC0000000
#define __KERNEL_MAX_ADDR   0xC8000000
#define __KERNEL_STACK      __KERNEL_MAX_ADDR
#define __KERNEL_STACK_SZ   0x2000

#define pa(X) ((X) - __KERNEL_BASE)

#endif
