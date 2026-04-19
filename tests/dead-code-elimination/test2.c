#include <stdio.h>

int main(int argc, const char * argv[]) {
    int x = 3;
    int a = x + 10;

    // Dead chain: each depends on the previous
    // DCE must kill all 4 in sequence as faintness propagates
    int b = a * 2;          // Dead L1: feeds only c
    int c = b + 7;          // Dead L2: feeds only d (killed when b is killed)
    int d = c - 1;          // Dead L3: feeds only e (killed when c is killed)
    int e = d * d;          // Dead L4: never used (killed when d is killed)

    int p;
    int q;
    if (a > 10) {
        p = a - 5;          // Live: reaches f
        q = c + p;          // Dead: q unused, but pulls live p into dead chain
                            // DCE must NOT kill p just because q is dead
    } else {
        p = a + 5;          // Live: reaches f
        q = d - p;          // Dead: same as above
    }

    // Another kill chain branching off a live value
    int r = p * 3;          // Dead: r feeds only s
    int s = r + x;          // Dead: s feeds only t (killed when r is killed)
    int t = s * s;          // Dead: never used (killed when s is killed)

    int f = p + x;          // Live: printed
    printf("%d\n", f);
    return 0;
}