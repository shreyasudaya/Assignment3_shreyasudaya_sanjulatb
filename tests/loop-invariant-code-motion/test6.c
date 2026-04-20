#include <stdio.h>

/**
 * loop_dependent: A negative test case for LICM.
 * The instruction 'x + i' depends on the loop induction variable 'i'.
 * It is NOT loop-invariant and should stay inside the loop.
 */
void loop_dependent(int x, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        // Should NOT be hoisted!
        int val = x + i; 
        total += val;
    }
    printf("Total: %d\n", total);
}

int main() {
    loop_dependent(10, 1000000);
    return 0;
}