#include <lib/icxxabi.h>
#include <lilac/panic.h>
#include <lilac/sync.h>

extern "C" {

void __cxa_pure_virtual(void)
{
    panic("__cxa_pure_virtual function called");
}

typedef struct {
    void (*destructor_func)(void*);
    void* obj_ptr;
    void* dso_handle;
} atexit_func_entry_t;


static atexit_func_entry_t __atexit_funcs[ATEXIT_MAX_FUNCS];
static unsigned __atexit_func_count = 0;


int __cxa_atexit(void (*f)(void*), void* objptr, void* dso)
{
    if (__atexit_func_count >= ATEXIT_MAX_FUNCS) {
        return -1;
    }
    __atexit_funcs[__atexit_func_count].destructor_func = f;
    __atexit_funcs[__atexit_func_count].obj_ptr = objptr;
    __atexit_funcs[__atexit_func_count].dso_handle = dso;
    __atexit_func_count++;
    return 0;
}

/* Run registered destructors. If f == nullptr, run all. */
void __cxa_finalize(void* f)
{
    for (unsigned i = __atexit_func_count; i > 0; --i) {
        atexit_func_entry_t* entry = &__atexit_funcs[i - 1];
        if (entry->destructor_func && (!f || f == (void*)entry->destructor_func)) {
            (*entry->destructor_func)(entry->obj_ptr);
            entry->destructor_func = 0;
        }
    }
}

/* --- Guard variables for function-local statics --- */
static spinlock_t __cxa_guard_lock = SPINLOCK_INIT;

int __cxa_guard_acquire(long long* g)
{
    acquire_lock(&__cxa_guard_lock);
    if (*g & 0x1) {
        release_lock(&__cxa_guard_lock);
        return 0;
    }
    *g |= 0x2;
    release_lock(&__cxa_guard_lock);
    return 1;   /* allow initialization */
}

void __cxa_guard_release(long long* g)
{
    acquire_lock(&__cxa_guard_lock);
    *g &= ~0x2;
    *g |= 0x1;
    release_lock(&__cxa_guard_lock);
}

void __cxa_guard_abort(long long* g)
{
    acquire_lock(&__cxa_guard_lock);
    *g &= ~0x2;
    release_lock(&__cxa_guard_lock);
}


} /* extern "C" */
