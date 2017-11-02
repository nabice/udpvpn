#ifndef _die_h_
#define _die_h_
#include <stdio.h>
void panic(char *);
void nopanic(char *);
#define LOG(fmt, ...) do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#endif
