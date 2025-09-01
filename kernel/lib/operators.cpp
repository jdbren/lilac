#include <cstddef>
#include <new>

extern "C" void* kmalloc(std::size_t);
extern "C" void  kfree(void*);

void* operator new(std::size_t n) noexcept(false) { return kmalloc(n); }
void* operator new[](std::size_t n) noexcept(false) { return ::operator new(n); }

void operator delete(void* p) noexcept { kfree(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }

// (C++14/17) sized delete (some compilers request these):
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }

// (C++17) aligned new/delete overloads if your toolchain mandates them:
void* operator new(std::size_t n, std::align_val_t) { return ::operator new(n); }
void  operator delete(void* p, std::align_val_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void  operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
