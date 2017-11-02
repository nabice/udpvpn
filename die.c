#include <stdio.h>
#include <stdlib.h>
#include "die.h"

void panic(char *s)
{
    printf("%s", s);
    exit(1);
}

void nopanic(char *s)
{
    printf("%s", s);
}
