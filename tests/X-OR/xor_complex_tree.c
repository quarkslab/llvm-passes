// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
// RUN: clang %s -O2 -o %t3.out
// RUN: test `%t2.out` = `%t3.out`
#include <stdio.h>
#include <stdint.h>

int main() {
    volatile uint32_t a = 0xffffffff, b = 0, c = 0xffffffef, d = 0xfeffffef, e = 0xffffffe7, f = 0xffffffef, g = 0xfeffffff;
    b=a^0xffffffff^c^d^e^f;
    uint32_t tmp = c ^ g;
    uint32_t tmp2 = b ^ tmp;
    printf("%u\n", tmp2);
    return 0;
}
