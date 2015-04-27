;; RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
;; RUN: test `grep -c ' xor ' %t1.ll` = 0
;; RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
;; RUN: clang %s -O3 -o %t3.out
;; RUN: test `%t2.out` = `%t3.out`
; ModuleID = 'xor_bool_1bit_openssl.ll'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [4 x i8] c"%u\0A\00", align 1

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %a = alloca i1, align 4
  store volatile i1 1, i1* %a, align 4
  %1 = load volatile i1* %a, align 4
  %2 = xor i1 %1, true
  %3 = xor i1 %2, true
  %4 = zext i1 %3 to i32

  %5 = call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([4 x i8]* @.str, i32 0, i32 0), i32 %4)
  ret i32 0
}

declare i32 @printf(i8*, ...) #1

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
