#include <stdio.h>

int test(int x) {
    int p = 0;
    for (int i = 0; i < 100; i++) {
        p = x * 2; // Loop invariant: x and 2 do not change in the loop
    }
    return p;
}

int main() {
    int r = test(3);
    printf("result is: %d\n", r);
    return 0;
}
