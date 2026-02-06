/**
 * @file syscall_internal.h
 * @brief Internal header declaring raw syscall wrapper functions.
 *
 * @details
 * The raw syscall wrappers (__syscall0 through __syscall6) are defined in
 * syscall.S (AArch64 assembly). This header provides their C prototypes so
 * that libc implementation files can call them without repeating the extern
 * declarations in every source file.
 *
 * This is an internal header — it should NOT be included by user programs.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern long __syscall0(long num);
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);
extern long __syscall6(long num, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);

#ifdef __cplusplus
}
#endif
