
int toupper(int ch)
{
    if (ch >= 'a' && ch <= 'z')
        return ch - 32;
    return ch;
}

int tolower(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch + 32;
    return ch;
}

int isprint(int ch)
{
    return ch >= 0x20 && ch <= 0x7E;
}

int isspace(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' ||
        ch == '\f' || ch == '\r';
}

int isdigit(int arg)
{
    return arg >= '0' && arg <= '9';
}

char isxdigit(char x)
{
    return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') ||
        (x >= 'A' && x <= 'F');
}

int atoi(const char *s)
{
    int result = 0;
    int sign = 1;

    while (*s == ' ' || *s == '\t' || *s == '\n' ||
           *s == '\r' || *s == '\f' || *s == '\v') {
        s++;
    }

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return sign * result;
}
