;; RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -S -emit-llvm -O2 -o %t1.ll
;; RUN: test `grep -c ' xor ' %t1.ll` = 0
;; RUN: clang -Xclang -load -Xclang LLVMX-OR.so %s -O2 -o %t2.out
;; RUN: test `%t2.out` = 1
; ModuleID = 'xor_bool2.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [2 x i8] c"0\00", align 1
@.str1 = private unnamed_addr constant [2 x i8] c"1\00", align 1

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %a = alloca i1, align 4
  %b = alloca i1, align 4
  store volatile i1 1, i1* %a, align 4
  store volatile i1 1, i1* %b, align 4
  %1 = load volatile i1* %a, align 4
  %2 = load volatile i1* %b, align 4
  %3 = xor i1 %1, %2
  %4 = icmp eq i1 %3, 1
  br i1 %4, label %5, label %7

; <label>:5                                       ; preds = %0
  %6 = tail call i32 @puts(i8* getelementptr inbounds ([2 x i8]* @.str, i64 0, i64 0)) #2
  br label %9

; <label>:7                                       ; preds = %0
  %8 = tail call i32 @puts(i8* getelementptr inbounds ([2 x i8]* @.str1, i64 0, i64 0)) #2
  br label %9

; <label>:9                                       ; preds = %6, %4
  ret i32 0
}

; Function Attrs: nounwind
declare i32 @puts(i8* nocapture readonly) #1

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }
