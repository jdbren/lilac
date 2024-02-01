#ifndef _CTYPE_H
#define _CTYPE_H

int toupper(int ch) {
    if (ch >= 'a' && ch <= 'z')
        return ch - 32;
    return ch;
}

#endif
