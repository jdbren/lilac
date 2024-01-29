#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

void heap_initialize(void);
void* kmalloc(int size);
void* kcalloc(int size);
void kfree(void *addr);

#endif
