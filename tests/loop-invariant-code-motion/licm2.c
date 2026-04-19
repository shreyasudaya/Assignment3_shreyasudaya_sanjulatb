// basic_hoist.c
#include <stdio.h>
#include <stdlib.h>

void basic_hoist(int x, int y, int n) {
    int result = 0;
    for (int i = 0; i < n; i++) {
        // x + y is loop invariant
        int val = x + y; 
        result += i * val;
    }
    printf("Result: %d\n", result);
}

int main(int argc, char** argv) {
    basic_hoist(10, 20, 1000000);
    return 0;
}