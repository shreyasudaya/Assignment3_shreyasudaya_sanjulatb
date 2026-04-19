int test(int a, int b, int c) {
    int x = 0;

    if (a > 0) {
        x = a + b;
        if (b > 0) {
            x = x * 2;
        } else {
            x = x + c;
        }
    } else {
        x = c - a;
    }

    int y = x + 1;

    while (y > 0) {
        y = y - b;
        if (y > c) {
            y = y / 2;
        }
    }

    return y + x;
}