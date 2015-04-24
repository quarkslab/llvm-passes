// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
// RUN: clang %s -O2 -o %t3.out
// RUN: test `%t2.out 10 20 25` = `%t3.out 10 20 25`
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    uint32_t a = atoi(argv[1]), b = atoi(argv[2]),
             c = 1000;

    printf("%u\n", (a^b)^c);
    return 0;
}
