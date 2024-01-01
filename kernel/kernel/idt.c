#include <kernel/idt.h>

#define IDT_SIZE 256
#define INT_GATE 0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

IDT idtptr;
