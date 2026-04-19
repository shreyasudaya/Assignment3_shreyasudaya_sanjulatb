// chain_hoist.c
#include <stdio.h>

void chain_hoist(int a, int b, int c, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        int x = a + b; // Invariant
        int y = x + c; // Invariant because x is invariant
        total += y + i;
    }
    printf("Total: %d\n", total);
}

int main() {
    chain_hoist(1, 2, 3, 1000000);
    return 0;
}