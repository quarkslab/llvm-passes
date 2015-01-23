// RUN: clang -Xclang -load -Xclang LLVMObfuscateZero.so %s -S -emit-llvm -O0 -o %t1.ll
// RUN: test `grep -c ' null' %t1.ll` = 2

#include <stdlib.h>

int main(int argc, char *argv[]) {
    int a = 10;
    volatile int *p = NULL;
    if(p == NULL)
        return 1;
    return 0;
}
