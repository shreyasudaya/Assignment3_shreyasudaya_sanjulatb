#include <stdio.h>

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