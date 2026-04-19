#include <stdio.h>

int main() {
    int i, j;

    for (i = 0; i < 3; i++) {
        printf("Outer loop i = %d\n", i);

        for (j = 0; j < 4; j++) {
            printf("  Inner loop j = %d\n", j);
        }
    }

    return 0;
}