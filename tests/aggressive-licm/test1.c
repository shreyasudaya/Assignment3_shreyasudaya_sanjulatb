#include <stdio.h>

int compute(int n, int *scale, int *offset) {
    int result = 0;
    for (int i = 0; i < n; i++) {
        result += (*scale) * (*offset);  // has store: result += ...
    }
    return result;
}

// Store-free loop — only reads, no writes inside
int compute_readonly(int n, int *scale, int *offset) {
    // No accumulator stored inside the loop
    if (n <= 0) return 0;
    return (*scale) * (*offset) * n;  // but this has no loop at all...
}

// Better: a search loop with no stores
int find_threshold(int n, int *limit, int *step) {
    int i = 0;
    for (; i < n; i += (*step)) {
        if (i >= (*limit)) break;   // no store inside loop
    }
    return i;
}

int main() {
    int scale = 7, offset = 3;
    int limit = 50, step = 3;
    printf("compute: %d\n", compute(1000, &scale, &offset));
    printf("threshold: %d\n", find_threshold(200, &limit, &step));
    return 0;
}