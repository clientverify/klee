; ModuleID = 'instructions.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.ucontext = type { i64, %struct.ucontext*, %struct.sigaltstack, %struct.mcontext_t}
%struct.sigaltstack = type { i8*, i32, i64 }
%struct.mcontext_t = type { [23 x i64], %struct._libc_fpstate*, [8 x i64] }
%struct._libc_fpstate = type { i16, i16, i16, i16, i64, i64, i32, i32, [8 x %struct._libc_fpxreg], [16 x %struct._libc_xmmreg], [24 x i32] }
%struct._libc_fpxreg = type { [4 x i16], i16, [3 x i16] }
%struct._libc_xmmreg = type { [4 x i32] }

define void @main () {
      ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5000(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = add nsw i64 %2, -8
  %4 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 15
  store i64 %3, i64* %4, align 8, !tbaa !1
  %5 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 10
  %6 = load i64* %5, align 8, !tbaa !1
  %7 = inttoptr i64 %3 to i64*
  store i64 %6, i64* %7, align 8, !tbaa !1
  %8 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %9 = load i64* %8, align 8, !tbaa !1
  %10 = add nsw i64 %9, 1
  %11 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %10, i64* %11, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5001(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 10
  store i64 %2, i64* %3, align 8, !tbaa !1
  %4 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %5 = load i64* %4, align 8, !tbaa !1
  %6 = add nsw i64 %5, 3
  %7 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %6, i64* %7, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5004(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 6
  store i64 123, i64* %1, align 8, !tbaa !1
  %2 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %3 = load i64* %2, align 8, !tbaa !1
  %4 = add nsw i64 %3, 7
  %5 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %4, i64* %5, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b500b(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 7
  store i64 123, i64* %1, align 8, !tbaa !1
  %2 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %3 = load i64* %2, align 8, !tbaa !1
  %4 = add nsw i64 %3, 7
  %5 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %4, i64* %5, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5012(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 4
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %4 = load i64* %3, align 8, !tbaa !1
  %5 = add nsw i64 %4, 16
  %6 = inttoptr i64 %5 to i64*
  %7 = load i64* %6, align 8, !tbaa !1
  %8 = add nsw i64 %7, %2
  %9 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 4
  store i64 %8, i64* %9, align 8, !tbaa !1
  %10 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %11 = load i64* %10, align 8, !tbaa !1
  %12 = add nsw i64 %11, 5
  %13 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %12, i64* %13, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5017(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = add nsw i64 %2, -8
  %4 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 15
  store i64 %3, i64* %4, align 8, !tbaa !1
  %5 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 6
  %6 = load i64* %5, align 8, !tbaa !1
  %7 = inttoptr i64 %3 to i64*
  store i64 %6, i64* %7, align 8, !tbaa !1
  %8 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %9 = load i64* %8, align 8, !tbaa !1
  %10 = add nsw i64 %9, 2
  %11 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %10, i64* %11, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b5019(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = inttoptr i64 %2 to i64*
  %4 = load i64* %3, align 8, !tbaa !1
  %5 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 1
  store i64 %4, i64* %5, align 8, !tbaa !1
  %6 = load i64* %1, align 8, !tbaa !1
  %7 = add nsw i64 %6, 8
  %8 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 15
  store i64 %7, i64* %8, align 8, !tbaa !1
  %9 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %10 = load i64* %9, align 8, !tbaa !1
  %11 = add nsw i64 %10, 2
  %12 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %11, i64* %12, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b501b(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 1
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = add nsw i64 %2, 1
  %4 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 1
  store i64 %3, i64* %4, align 8, !tbaa !1
  %5 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %6 = load i64* %5, align 8, !tbaa !1
  %7 = add nsw i64 %6, 3
  %8 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %7, i64* %8, align 8, !tbaa !1
  ret void
}

; Function Attrs: nounwind uwtable
define void @interp_fn_5b501e(%struct.ucontext* nocapture readonly %begin_ctx, %struct.ucontext* nocapture %end_ctx) #0 {
  %1 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 15
  %2 = load i64* %1, align 8, !tbaa !1
  %3 = add nsw i64 %2, -8
  %4 = inttoptr i64 %3 to i64*
  %5 = load i64* %4, align 8, !tbaa !1
  %6 = add nsw i64 %5, 1
  store i64 %6, i64* %4, align 8, !tbaa !1
  %7 = getelementptr inbounds %struct.ucontext* %begin_ctx, i64 0, i32 3, i32 0, i64 16
  %8 = load i64* %7, align 8, !tbaa !1
  %9 = add nsw i64 %8, 5
  %10 = getelementptr inbounds %struct.ucontext* %end_ctx, i64 0, i32 3, i32 0, i64 16
  store i64 %9, i64* %10, align 8, !tbaa !1
  ret void
}

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = metadata !{metadata !"Ubuntu clang version 3.4-1ubuntu3 (tags/RELEASE_34/final) (based on LLVM 3.4)"}
!1 = metadata !{metadata !2, metadata !2, i64 0}
!2 = metadata !{metadata !"long long", metadata !3, i64 0}
!3 = metadata !{metadata !"omnipotent char", metadata !4, i64 0}
!4 = metadata !{metadata !"Simple C/C++ TBAA"}
