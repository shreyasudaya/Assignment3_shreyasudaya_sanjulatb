#include <stdio.h>
#include <stdlib.h>

//External function with unknown side effects - forces call to be preserved
int __attribute__((noinline)) sideEffectFunc(int x) {
    return x * 2;
}

int main(int argc, const char * argv[]) {
    int x = 5;
    int a = x + 10;

    int callResult = sideEffectFunc(a);     // call preserved (side effect) 
                                            // callResult is live, but c (which depends on it) is dead

    volatile int dbgProbe = a;              // volatile access: mayHaveSideEffects
                                            // must be preserved regardless of use

    int arr[3] = {0, 0, 0};
    arr[0] = a * 2;                         // store: void-typed, always live
    arr[1] = a + 7;                         // store: void-typed, always live

    int b = a * 3;                          // Dead L1
    int c = b + callResult;                 // Dead L2: depends on dead b
                                            // callResult is live (call preserved)
                                            // but c itself is dead
    int d = c - dbgProbe;                   // Dead L3: depends on dead c
    int e = d * d;                          // Dead L4: depends on dead d

    int result = a + x;                     // Live: fed into printf

    printf("%d\n", result);                 // mayHaveSideEffects: preserved
    return 0;                               // isTerminator: preserved
}