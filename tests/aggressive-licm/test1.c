int aggressive_test(int n) {
    int arr[1] = {0};
    int factor_val = 10;
    int *factor = &factor_val;

    for (int i = 0; i < n; i++) {
        // Aggressive LICM should hoist the load of *factor 
        // and the GEP for arr[0]
        arr[0] += (*factor) + i;
    }
    return arr[0];
}

int main() {
    return aggressive_test(100);
}