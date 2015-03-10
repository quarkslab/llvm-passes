// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
// RUN: clang %s -O2 -o %t3.out
// RUN: test `%t2.out 10 20 25 100` = `%t3.out 10 20 25 100`
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    uint32_t a = atoi(argv[1]), b = 0, e = atoi(argv[3]), f = atoi(argv[4]), g = 42;
    uint64_t c = atol(argv[2]), d = 1000;

    printf("%ld\n", (a^b^e) + (a^b^e)^(c^d) + (f^g));
    return 0;
}
