// RUN: clang -Xclang -load -Xclang LLVMSplitBitwiseOp.so %s -S -emit-llvm -O0 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` -gt 1
// RUN: clang -Xclang -load -Xclang LLVMSplitBitwiseOp.so %s -O0 -o %t2.out
// RUN: clang %s -O0 -o %t3.out
// RUN: test `%t2.out` = `%t3.out`
#include <stdio.h>
#include <stdint.h>

int main() {
    volatile uint64_t a = 0xffffffffffffffff, b = 0;
    b=a^0xffffffffffffffff;
    printf("%ld\n", b);
    return 0;
}
