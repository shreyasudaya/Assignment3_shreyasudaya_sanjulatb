#include <stdio.h>

/**
 * nested_hoist: Testing multi-level hoisting.
 * x + y should ideally end up in the outer loop's preheader.
 */
int nested_hoist(int x, int y, int N, int M) {
    int total = 0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            // Invariant for both loops
            int invariant = x + y; 
            total += invariant + j;
        }
    }
    return total;
}

int main() {
    int N = 100;
    int M = 100;
    int x = 10;
    int y = 20;

    int final_total = nested_hoist(x, y, N, M);

    printf("Nested Hoist Result: %d\n", final_total);

    return 0;
}