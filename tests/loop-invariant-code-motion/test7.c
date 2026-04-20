#include <stdio.h>

/**
 * exit_dominance: Tests if the pass respects control flow.
 * 'x * y' is invariant but is NOT guaranteed to execute.
 * A "safe" LICM should not hoist this unless it can prove the 
 * block is always entered (which it isn't here).
 */
void exit_dominance(int x, int y, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        // This condition is never true for i >= 0
        if (i < 0) { 
            int val = x * y; 
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