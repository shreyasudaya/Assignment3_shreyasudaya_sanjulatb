; ModuleID = 'build/tests/licm-opt.bc'
source_filename = "tests/licm.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [15 x i8] c"result is: %d\0A\00", align 1

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @test(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 0, ptr %4, align 4
  %5 = load i32, ptr %2, align 4
  %6 = mul nsw i32 %5, 2
  %.promoted = load i32, ptr %4, align 4
  %.promoted1 = load i32, ptr %3, align 1
  br label %7

7:                                                ; preds = %12, %1
  %8 = phi i32 [ %6, %12 ], [ %.promoted1, %1 ]
  %9 = phi i32 [ %13, %12 ], [ %.promoted, %1 ]
  %10 = icmp slt i32 %9, 100
  br i1 %10, label %11, label %14

11:                                               ; preds = %7
  br label %12

12:                                               ; preds = %11
  %13 = add nsw i32 %9, 1
  br label %7, !llvm.loop !6

14:                                               ; preds = %7
  %.lcssa2 = phi i32 [ %8, %7 ]
  %.lcssa = phi i32 [ %9, %7 ]
  store i32 %.lcssa, ptr %4, align 4
  store i32 %.lcssa2, ptr %3, align 1
  %15 = load i32, ptr %3, align 4
  ret i32 %15
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  %3 = call i32 @test(i32 noundef 3)
  store i32 %3, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %4)
  ret i32 0
}

declare i32 @printf(ptr noundef, ...) #1

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

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
