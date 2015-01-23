// RUN: clang -Xclang -load -Xclang LLVMObfuscateZero.so %s -S -emit-llvm -O2 -o %t1.ll
// RUN: test `grep -c ' ret i32 0' %t1.ll` = 0

int main(int argc, char *argv[]) {
    int a = argc;

    return 0;
}
