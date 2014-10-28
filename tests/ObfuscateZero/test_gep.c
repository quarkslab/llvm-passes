// RUN: clang -Xclang -load -Xclang ~/epona/llvm-pass/build/llvm-passes/LLVMObfuscateZero.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' ret i32 0' %t1.ll` = 0

#include <stdlib.h>

struct f { 
    int a;
    float b;
};

void foo(struct f* s) {
    exit(s->b);
}


int main(int argc, char* argv[]) {
    int a = 0;
    struct f *test = malloc(10 * sizeof(struct f));
    test[argc].a = 10; 
    test[argc + 1].b = 100;
    foo(test + argc + 1);
    return 0;
}
