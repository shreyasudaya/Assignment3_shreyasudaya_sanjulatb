int test(int condition, int a, int b) {
    int result = 100; // Default value
    
    // Invariant calculation: (a + b)
    while (condition) {
        result = (a + b); 
        condition = 0; // Run once and exit
    }

    return result; // We expect 100 if condition is false, or (a+b) if true
}