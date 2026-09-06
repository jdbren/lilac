#include <lilac/percpu.h>

bool smp_enabled = false;

void smp_init(void)
{
#ifdef CONFIG_SMP
    ap_init();
    smp_enabled = true;
#endif
}
