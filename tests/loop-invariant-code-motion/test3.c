#include <stdio.h>

// Define the size as a constant
#define ITERATIONS 100000

int results[ITERATIONS];

/**
TRANSITIVE INVARIANCE TEST
 */
void test_hoisting(int a, int b, int iterations, int *out) {
    for (int i = 0; i < iterations; i++) {
        // --- INVARIANT CHAIN ---
        int x = a + b;           
        int y = x * 5;           
        int z = y + 10;          
        
        out[i] = z + i; 
    }
}

int main() {
    // Call the hoisting test using the global array
    test_hoisting(1, 2, ITERATIONS, results);

    // Print the final result to verify correctness
    printf("Final result at index %d: %d\n", ITERATIONS - 1, results[ITERATIONS - 1]);

    return 0;
}