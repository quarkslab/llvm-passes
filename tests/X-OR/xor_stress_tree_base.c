// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
// RUN: clang %s -O2 -o %t3.out
// RUN: test `%t2.out 10` = `%t3.out 10`
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    volatile uint32_t a = atoi(argv[1]),
                      b = 4,
                      c = 0xfffffffb,
                      d = 42,
                      e = 89,
                      f = 0xfffeffff,
                      g = 20145,
                      h = 1,
                      i = 789,
                      j = 45678,
                      k = 987654;

    printf("%u\n", ((a^b)^c)^(((d^e)^(f^g))^((h^i)^(j^k))));
    return 0;
}
