// memory_load.c
#include <stdio.h>

void memory_load(int *ptr, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        // If *ptr is changed in the loop, this cannot be hoisted
        int val = *ptr + 5; 
        total += val;
        if (i % 2 == 0) *ptr = i; 
    }
    printf("Total: %d\n", total);
}

int main() {
    int x = 10;
    memory_load(&x, 1000000);
    return 0;
}