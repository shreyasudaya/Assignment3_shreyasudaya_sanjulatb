// exit_dominance.c
#include <stdio.h>

void exit_dominance(int x, int y, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (i < 0) { // Condition never met
            int val = x * y; // Invariant but not on guaranteed path
            total += val;
        }
        total += i;
    }
    printf("Total: %d\n", total);
}

int main() {
    exit_dominance(10, 20, 1000000);
    return 0;
}