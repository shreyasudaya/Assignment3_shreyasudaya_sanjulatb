#include <stdio.h>

int main() {
    int i, j, k;

    int A = 10;          // invariant for all loops
    int B = 5;           // invariant for all loops

    for (i = 0; i < 3; i++) {

        int Ci = i * 2;  // invariant w.r.t j and k (but NOT i-loop invariant)

        int outerConst = A + B; // invariant w.r.t ALL loops (true LICM candidate)

        printf("Outer loop i=%d, outerConst=%d\n", i, outerConst);

        for (j = 0; j < 4; j++) {

            int Cj = Ci + 3; // invariant w.r.t k loop only

            int midConst = A * B; // invariant w.r.t ALL inner loops

            printf("  Middle loop j=%d, midConst=%d\n", j, midConst);

            for (k = 0; k < 5; k++) {

                int Ck = Cj + 7; // invariant within innermost loop

                int innerConst = A + B + Ci; // invariant w.r.t k loop

                printf("    Inner loop k=%d, Ck=%d\n", k, Ck);
            }
        }
    }

    return 0;
}