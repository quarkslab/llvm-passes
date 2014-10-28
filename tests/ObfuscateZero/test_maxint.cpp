// RUN: clang++ -std=c++11 -Xclang -load -Xclang ~/epona/llvm-pass/build/llvm-passes/LLVMObfuscateZero.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: clang++ -std=c++11 -Xclang -load -Xclang ~/epona/llvm-pass/build/llvm-passes/LLVMObfuscateZero.so %s -O2 -o %t1.exe
// RUN: clang++ -std=c++11 %s -S -emit-llvm -O2 -o %t2.exe
// RUN: test `grep -c ' ret i32 0' %t1.ll` = 0
// RUN: test `%t1.exe` = `%t2.exe`
#include <iostream>
#include <limits>
#include <cassert>
#include <cstdint>

uint64_t foo(uint64_t x) {
    volatile uint64_t a = x;
    return 0;
}

int main() {
    volatile uint64_t a = std::numeric_limits<uint64_t>::max();
    return foo(a);
}
