#include <stdio.h>
#include <stdlib.h>

// External function with unknown side effects - forces call to be preserved
int __attribute__((noinline)) sideEffectFunc(int x) {
    return x * 2;
}

int main(int argc, const char * argv[]) {

    // -------------------------------------------------------------------------
    // CASE 1: isTerminator() - return/branch must never be removed
    // The return 0 at end is the terminator. If DCE wrongly killed it,
    // the function would have no exit. Verified implicitly by program running.
    // -------------------------------------------------------------------------

    int x = 5;
    int a = x + 10;

    // -------------------------------------------------------------------------
    // CASE 2: mayHaveSideEffects() - call instruction must be preserved
    // even though its return value is never used (dead result, live call)
    // DCE must keep the call but CAN remove the dead result assignment
    // -------------------------------------------------------------------------
    int callResult = sideEffectFunc(a);     // call preserved (side effect)
                                            // callResult itself is dead

    // -------------------------------------------------------------------------
    // CASE 3: isa<DbgInfoIntrinsic> - debug intrinsics always preserved
    // Compile with -g to emit these. Represented here as a volatile read
    // which also triggers mayHaveSideEffects()
    // -------------------------------------------------------------------------
    volatile int dbgProbe = a;              // volatile access: mayHaveSideEffects
                                            // must be preserved regardless of use

    // -------------------------------------------------------------------------
    // CASE 4: getType()->isVoidTy() - void instructions always preserved
    // Store is void-typed: even if the stored value is never loaded,
    // the store itself must stay (observable memory effect)
    // -------------------------------------------------------------------------
    int arr[3] = {0, 0, 0};
    arr[0] = a * 2;                         // store: void-typed, always live
    arr[1] = a + 7;                         // store: void-typed, always live
    // arr values never read — but stores must NOT be removed

    // -------------------------------------------------------------------------
    // Dead kill chain that hangs off the live nodes above
    // DCE should kill all of these since none feed into printf or return
    // -------------------------------------------------------------------------
    int b = a * 3;                          // Dead L1
    int c = b + callResult;                 // Dead L2: depends on dead b
                                            // callResult is live (call preserved)
                                            // but c itself is dead
    int d = c - dbgProbe;                   // Dead L3: depends on dead c
    int e = d * d;                          // Dead L4: depends on dead d

    // -------------------------------------------------------------------------
    // Live chain that must survive
    // -------------------------------------------------------------------------
    int result = a + x;                     // Live: fed into printf

    printf("%d\n", result);                 // mayHaveSideEffects: preserved
    return 0;                               // isTerminator: preserved
}