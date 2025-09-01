#include <iostream>
#include <sys/syscall.h>

using namespace std;

int main()
{
    cout << "Calling shutdown..." << endl;
    return syscall1(SYS_reboot, 1);
}
