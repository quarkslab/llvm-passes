//RUN : clang -Xclang -load -Xclang ./LLVMObfuscateVar.so  -S basic.c -emit-llvm -O0 -o basic.ll
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int x = 3;
  int offset = 0;
  char * pEnd;
  if(argc>1){
    offset = strtol(argv[1],&pEnd,10);
  }
  x+=offset;
  /* int b = 3; */
  /* b+=3; */
  printf("x=%d\n",x);
  return 0;
}

