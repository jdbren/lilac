#ifndef _CTYPE_H
#define _CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int toupper(int ch);
int tolower(int ch);

int isprint(int c);
int isspace(int c);
int isdigit(int arg);
char isxdigit(char x);


#ifdef __cplusplus
}
#endif

#endif
