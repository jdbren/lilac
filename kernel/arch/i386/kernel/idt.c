#include <stdint.h>

#include <kernel/idt.h>

typedef struct IDTGate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) IDTGate;

typedef struct IDT {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) IDT;

#define IDT_SIZE 256
#define INT_GATE 0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

IDT idtptr;
