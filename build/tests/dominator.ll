; ModuleID = 'build/tests/dominator-opt.bc'
source_filename = "tests/dominator.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 1, ptr %3, align 4
  br label %4

4:                                                ; preds = %8, %0
  %5 = load i32, ptr %3, align 4
  %6 = add nsw i32 %5, 1
  store i32 %6, ptr %3, align 4
  %7 = icmp slt i32 %5, 100
  br i1 %7, label %8, label %17

8:                                                ; preds = %4
  store i32 0, ptr %2, align 4
  %9 = load i32, ptr %2, align 4
  %10 = add nsw i32 %9, 2
  store i32 %10, ptr %2, align 4
  %11 = load i32, ptr %2, align 4
  %12 = add nsw i32 %11, 3
  store i32 %12, ptr %2, align 4
  %13 = load i32, ptr %2, align 4
  %14 = add nsw i32 %13, 4
  store i32 %14, ptr %2, align 4
  %15 = load i32, ptr %2, align 4
  %16 = sdiv i32 %15, 2
  store i32 %16, ptr %2, align 4
  br label %4, !llvm.loop !6

17:                                               ; preds = %4
  %18 = load i32, ptr %2, align 4
  ret i32 %18
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 21.1.8 (https://github.com/llvm/llvm-project 2078da43e25a4623cab2d0d60decddf709aaea28)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
