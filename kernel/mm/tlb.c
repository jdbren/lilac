#include <mm/tlb.h>
#include <lilac/boot.h>
#include <lilac/config.h>
#include <lilac/interrupt.h>
#include <lilac/percpu.h>
#include <lilac/sched.h>
#include <lilac/sync.h>
#include <lilac/rwsem.h>
#include <lib/list.h>

struct tlb_shootdown {
    struct tlb_inval *tlb;
    struct task *task;
    volatile atomic_long pending;
    struct list_head list;
};

static DECLARE_RWSEM(sd_queue_lock);
static LIST_HEAD(shootdown_queue);

__isr_handler void tlb_shootdown_handler(void *frame)
{
    klog(LOG_DEBUG, "Received TLB shootdown IPI on CPU %d\n", this_cpu_id());
    while (!down_read_trylock(&sd_queue_lock))
        __pause();
    struct tlb_shootdown *sd;
    list_for_each_entry(sd, &shootdown_queue, list) {
        struct tlb_inval *tlb = sd->tlb;
        if (tlb->mm == current->mm && (sd->pending & (1 << this_cpu_id()))) {
            arch_tlb_flush_mmu(sd->tlb);
        }
        sd->pending &= ~(1 << this_cpu_id());
    }
    up_read(&sd_queue_lock);
    arch_eoi();
}

void init_tlb_shootdown(void)
{
    install_isr(TLB_SHOOTDOWN_VECTOR, tlb_shootdown_handler);
}

void tlb_shootdown(struct tlb_inval *tlb, struct task *task)
{
    struct tlb_shootdown sd = {
        .tlb = tlb,
        .task = task,
        .pending = (1UL << boot_info.ncpus) - 1,
    };
    while (!down_write_trylock(&sd_queue_lock))
        __pause();
    list_add_tail(&sd.list, &shootdown_queue);
    up_write(&sd_queue_lock);

    klog(LOG_DEBUG, "Initiating TLB shootdown for mm %p, pending mask: %lx\n",
         tlb->mm, atomic_load(&sd.pending));
    arch_broadcast_others_ipi(TLB_SHOOTDOWN_VECTOR);

    arch_tlb_flush_mmu(tlb);
    sd.pending &= ~(1 << this_cpu_id());

    klog(LOG_DEBUG, "Waiting for TLB shootdown completion, pending mask: %lx\n",
         atomic_load(&sd.pending));

    do {
        __pause();
    } while (sd.pending);

    klog(LOG_DEBUG, "TLB shootdown completed for mm %p\n", tlb->mm);

    while (!down_write_trylock(&sd_queue_lock))
        __pause();
    list_del(&sd.list);
    up_write(&sd_queue_lock);
}
