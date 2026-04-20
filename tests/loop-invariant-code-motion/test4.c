#include <stdio.h>

/**
 * If the pass is iterative, it will first hoist val1, 
 * then realize val2 is also invariant and hoist it in the next iteration.
 */
int simple_hoist(int a, int b, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        // INVARIANT 1
        int val1 = a + b; 
        // INVARIANT 2 (Depends on val1)
        int val2 = val1 * 2; 
        
        sum += val2 + i;
    }
    return sum;
}

int main() {
    int n = 1000000;
    int a = 5;
    int b = 10;

    int total_sum = simple_hoist(a, b, n);
    printf("Result: %d\n", total_sum);

    return 0;
}