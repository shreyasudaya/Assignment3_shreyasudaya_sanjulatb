#include <stdio.h>

/**
 * To test iterative hoisting, we create a chain of dependencies.
 * a and b are arguments (invariant).
 * x depends on a and b.
 * y depends on x.
 * z depends on y.
 */
void test_hoisting(int a, int b, int iterations, int *out) {
    for (int i = 0; i < iterations; i++) {
        // --- INVARIANT CHAIN ---
        int x = a + b;          // Level 1: Invariant (uses arguments)
        int y = x * 5;          // Level 2: Invariant (uses x)
        int z = y + 10;         // Level 3: Invariant (uses y)
        
        // We use 'z' and the loop index 'i' to ensure 
        // the calculations are not optimized away by DCE.
        out[i] = z + i; 
    }
}

int main() {
    int results[10];
    test_hoisting(1, 2, 10, results);
    for(int i = 0; i < 10; i++) {
        printf("%d ", results[i]);
    }
    return 0;
}
