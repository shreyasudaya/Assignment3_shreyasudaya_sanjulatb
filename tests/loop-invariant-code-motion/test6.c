#include <stdio.h>

// Tests: Nested loop hoisting
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