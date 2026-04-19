// nested_hoist.c
#include <stdio.h>

void nested_hoist(int x, int y, int N, int M) {
    int total = 0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            // Invariant for both loops
            int invariant = x * y; 
            total += invariant + j;
        }
    }
    printf("Total: %d\n", total);
}

int main() {
    nested_hoist(5, 10, 1000, 1000);
    return 0;
}