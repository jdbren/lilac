#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#define PAGE_SIZE 4096
#define __KERNEL_BASE 0xC0000000
#define pa(X) ((X) - __KERNEL_BASE)

#endif
