// loop_dependent.c
#include <stdio.h>

void loop_dependent(int x, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        // Should NOT be hoisted because it depends on i
        int val = x + i; 
        total += val;
    }
    printf("Total: %d\n", total);
}

int main() {
    loop_dependent(10, 1000000);
    return 0;
}