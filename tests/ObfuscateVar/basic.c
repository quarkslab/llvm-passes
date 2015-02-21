//RUN : clang -Xclang -load -Xclang ./LLVMObfuscateVar.so  -S basic.c -emit-llvm -O2 -o basic.ll
#include <stdio.h>
int main(int argc, char *argv[]) {
  int a = argc;
  
  //volatile int b = 3;
  a+=2;
  //a+=3;
  //b+=3;
  //a+=0;
  //volatile int b = 100;
  //b*=10;
  printf("a=%d\n",a);
  return 0;
}
