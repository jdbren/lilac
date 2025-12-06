#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/wait.h>

int main()
{
    char name[64] = {0};
    setsid();

    int fd = open("/dev/tty0", O_RDWR);
    dup(fd);
    dup(fd);

    write(1, "Enter name: ", 12);
    int count = read(0, &name, 63);
    if (count > 0 && name[count - 1] == '\n')
        name[count - 1] = '\0';
    else
        name[count] = '\0';
    setenv("USER", name, 1);

    struct termios tio;
    tcgetattr(0, &tio);

    /* input modes */
    tio.c_iflag = ICRNL | IXON | BRKINT | IMAXBEL;

    /* output modes */
    tio.c_oflag = OPOST | ONLCR;

    /* control modes */
    tio.c_cflag = CREAD | CS8 | HUPCL;

    /* local modes (canonical + echo enabled) */
    tio.c_lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK;

    /* control chars */
    tio.c_cc[VINTR]  = 0x03;  /* ^C */
    tio.c_cc[VQUIT]  = 0x1c;  /* ^\ */
    tio.c_cc[VERASE] = 0x7f;  /* DEL */
    tio.c_cc[VKILL]  = 0x15;  /* ^U */
    tio.c_cc[VEOF]   = 0x04;  /* ^D */
    tio.c_cc[VEOL]   = 0;
    tio.c_cc[VSUSP]  = 0x1a;  /* ^Z */

    tcsetattr(0, TCSANOW, &tio);

    setenv("TERM", "vt220", 1);
    setenv("HOME", "/root", 1);
    setenv("SHELL", "/bin/dash", 1);

    // Try to run a shell
    execl("/bin/bash", "-", NULL); // will not work yet
    execl("/bin/dash", "sh", "-lE", NULL);
    execl("/sbin/gush", "gush", NULL); // my custom shell
    return 1;
}
