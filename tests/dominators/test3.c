int test3(int n) {
    int sum = 0;
    int i = 0;

    while (i < n) {
        if (i % 3 == 0) {
            sum += i;
        } else {
            sum += 2 * i;
        }
        i++;
    }

    // Unreachable block
    if (0) {
        sum = -999;
    }

    return sum;
}