#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>

using namespace std;

int main()
{
    cout << "Calling shutdown..." << endl;
    return syscall(SYS_reboot, 1);
}
