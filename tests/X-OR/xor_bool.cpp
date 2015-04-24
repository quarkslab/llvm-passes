// RUN: clang++ -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O1 -o %t1.ll
// RUN: test `grep -c ' xor ' %t1.ll` = 0
// RUN: clang++ -Xclang -load -Xclang LLVMX-OR.so %s -O1 -o %t2.out
// RUN: test `%t2.out` = 0
#include <iostream>

int main() {
    volatile bool a = true, b = false;
    if(a^b)
        std::cout << 0 << std::endl;
    else
        std::cout << 1 << std::endl;
    return 0;
}
