// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
// RUN: test `%t2.out` = 150003
#include <stdio.h>
#include <stdint.h>

int main() {
    volatile uint64_t a = 3, b = 1, c = 0;
    b=a^150000;
    c=b+1;
    printf("%ld\n", b);
    return 0;
}
