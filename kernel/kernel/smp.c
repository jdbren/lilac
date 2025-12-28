#include <lilac/percpu.h>

void smp_init(void)
{
#ifdef CONFIG_SMP
    ap_init();
#endif
}
