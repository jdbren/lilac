// cpptest.cpp
#include <lilac/types.h>
#include <lilac/libc.h>

#ifdef DEBUG_CPP_INTEGRATION
// ------------------------------------------------------
// Test C++ objects and constructors
// ------------------------------------------------------

struct TestGlobal {
    TestGlobal() { puts("TestGlobal constructed\n"); }
    ~TestGlobal() { puts("TestGlobal destructed\n"); }
    int value = 42;
};

struct TestLocal {
    TestLocal() { puts("TestLocal constructed\n"); }
    ~TestLocal() { puts("TestLocal destructed\n"); }
    int value = 99;
};

// global static object
static TestGlobal g1;
static TestGlobal g2;

// ------------------------------------------------------
// C++ test function
// ------------------------------------------------------

extern "C" void cpptest() {
    puts("=== cpptest start ===\n");

    // function-local static (tests __cxa_guard_acquire/release)
    static TestLocal l1;
    static TestLocal l2;

    printf("Global g1.value=%d, g2.value=%d\n", g1.value, g2.value);
    printf("Local l1.value=%d, l2.value=%d\n", l1.value, l2.value);

    // simple dynamic allocation test
    int* p = new int(123);
    printf("Dynamic int=%d\n", *p);
    delete p;

    puts("=== cpptest end ===\n");
}
#endif
