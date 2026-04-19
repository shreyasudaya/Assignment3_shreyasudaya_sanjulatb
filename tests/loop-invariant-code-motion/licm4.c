// conditional_safety.c
#include <stdio.h>

void conditional_safety(int x, int y, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (n > 0) {
            // Technically invariant, but only safe if n > 0 is guaranteed
            int val = x / y; 
            total += val + i;
        }
    }
    printf("Total: %d\n", total);
}

int main() {
    conditional_safety(100, 2, 1000000);
    return 0;
}