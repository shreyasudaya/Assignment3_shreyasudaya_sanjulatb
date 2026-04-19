int test2(int x) {
    int a = 0;
    int b = 1;

    if (x > 0) {
        a = a + 2;

        if (x % 2 == 0) {
            b = b * 3;
        } else {
            b = b + 5;
        }

    } else {
        a = a - 1;
    }

    int c = a + b;

    return c;
}