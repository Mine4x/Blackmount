#include <stdio.h>
#include <stdlib.h>

int main() {
    int *a = malloc(sizeof(int));
    if (!a) { printf("malloc failed\n"); return 1; }
    *a = 42;
    printf("a = %d\n", *a);
    free(a);

    char *str = malloc(10);
    if (!str) return 1;
    for (int i = 0; i < 9; i++) str[i] = 'a' + i;
    str[9] = '\0';
    printf("str = %s\n", str);
    free(str);

    printf("Passed malloc test!\n");

    return 0;
}