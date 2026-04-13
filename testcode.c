#include <stdio.h>

int main() {
    int x = 0;
    for (int i = 0; i < 100; i++) {
        x += i;
    }
    printf("%d\n", x);
    return 0;
}