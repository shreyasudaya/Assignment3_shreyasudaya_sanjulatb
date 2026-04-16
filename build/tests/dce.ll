; ModuleID = 'build/tests/dce-opt.bc'
source_filename = "tests/dce.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main(i32 noundef %0, ptr noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  %12 = alloca i32, align 4
  store i32 0, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  store ptr %1, ptr %5, align 8
  store i32 1, ptr %6, align 4
  %13 = load i32, ptr %6, align 4
  %14 = add nsw i32 %13, 50
  store i32 %14, ptr %7, align 4
  %15 = load i32, ptr %7, align 4
  %16 = add nsw i32 %15, 96
  store i32 %16, ptr %8, align 4
  %17 = load i32, ptr %7, align 4
  %18 = icmp sgt i32 %17, 50
  br i1 %18, label %19, label %24

19:                                               ; preds = %2
  %20 = load i32, ptr %7, align 4
  %21 = sub nsw i32 %20, 50
  store i32 %21, ptr %9, align 4
  %22 = load i32, ptr %7, align 4
  %23 = mul nsw i32 %22, 96
  store i32 %23, ptr %10, align 4
  br label %29

24:                                               ; preds = %2
  %25 = load i32, ptr %7, align 4
  %26 = add nsw i32 %25, 50
  store i32 %26, ptr %9, align 4
  %27 = load i32, ptr %7, align 4
  %28 = mul nsw i32 %27, 96
  store i32 %28, ptr %10, align 4
  br label %29

29:                                               ; preds = %24, %19
  store i32 -46, ptr %11, align 4
  %30 = load i32, ptr %11, align 4
  %31 = load i32, ptr %9, align 4
  %32 = add nsw i32 %30, %31
  store i32 %32, ptr %12, align 4
  ret i32 0
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
