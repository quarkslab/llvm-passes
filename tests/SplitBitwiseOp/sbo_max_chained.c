// RUN: clang -Xclang -load -Xclang LLVMSplitBitwiseOp.so %s -S -emit-llvm -O0 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` -gt 2
// RUN: test `grep -c ' and ' %t1.ll` -gt 2
// RUN: test `grep -c ' or ' %t1.ll` -gt 2
// RUN: clang -Xclang -load -Xclang LLVMSplitBitwiseOp.so %s -O0 -o %t2.out
// RUN: clang %s -O0 -o %t3.out
// RUN: test `%t2.out` = `%t3.out`
#include <stdio.h>
#include <stdint.h>

int main() {
    volatile uint32_t a = 0xffffffff, b = 0, c = 0xffffffef, d = 0xfeffffef, e = 0xffffffe7, f = 0xffffffef, g = 0xfeffffff;
    b=a|0xffffffff&c^d^e&f|g;
    printf("%u\n", b);
    return 0;
}
