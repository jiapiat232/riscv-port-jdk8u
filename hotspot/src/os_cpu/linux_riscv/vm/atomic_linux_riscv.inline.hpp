/*
 * Copyright (c) 1999, 2023, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020, 2023, Huawei Technologies Co., Ltd.
 * All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef OS_CPU_LINUX_RISCV_VM_ATOMIC_LINUX_RISCV_INLINE_HPP
#define OS_CPU_LINUX_RISCV_VM_ATOMIC_LINUX_RISCV_INLINE_HPP

#include "runtime/atomic.hpp"
#include "runtime/os.hpp"
#include "vm_version_riscv.hpp"

// Implementation of class atomic for RISC-V.
//
// AMO Iron Rule: add and xchg MUST use single-instruction AMO (amoadd / amoswap with .aqrl)
// to prevent LL/SC livelock and cache-line starvation under high core counts.

#define FULL_MEM_BARRIER  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define READ_MEM_BARRIER  __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define WRITE_MEM_BARRIER __atomic_thread_fence(__ATOMIC_RELEASE)

inline void Atomic::store    (jbyte    store_value, jbyte*    dest) { *dest = store_value; }
inline void Atomic::store    (jshort   store_value, jshort*   dest) { *dest = store_value; }
inline void Atomic::store    (jint     store_value, jint*     dest) { *dest = store_value; }
inline void Atomic::store_ptr(intptr_t store_value, intptr_t* dest) { *dest = store_value; }
inline void Atomic::store_ptr(void*    store_value, void*     dest) { *(void**)dest = store_value; }

inline void Atomic::store    (jbyte    store_value, volatile jbyte*    dest) { *dest = store_value; }
inline void Atomic::store    (jshort   store_value, volatile jshort*   dest) { *dest = store_value; }
inline void Atomic::store    (jint     store_value, volatile jint*     dest) { *dest = store_value; }
inline void Atomic::store_ptr(intptr_t store_value, volatile intptr_t* dest) { *dest = store_value; }
inline void Atomic::store_ptr(void*    store_value, volatile void*     dest) { *(void* volatile *)dest = store_value; }

inline void Atomic::store    (jlong    store_value, jlong*    dest) { *dest = store_value; }
inline void Atomic::store    (jlong    store_value, volatile jlong*    dest) { *dest = store_value; }

inline jlong Atomic::load(volatile jlong* src) { return *src; }

// Atomic::add using AMO (amoadd.w.aqrl)
inline jint Atomic::add(jint add_value, volatile jint* dest) {
  jint old_value;
  __asm__ __volatile__ ("amoadd.w.aqrl %0, %2, %1"
                        : "=r" (old_value), "+A" (*dest)
                        : "r" (add_value)
                        : "memory");
  return old_value + add_value;
}

// Atomic::add_ptr using AMO (amoadd.d.aqrl)
inline intptr_t Atomic::add_ptr(intptr_t add_value, volatile intptr_t* dest) {
  intptr_t old_value;
  __asm__ __volatile__ ("amoadd.d.aqrl %0, %2, %1"
                        : "=r" (old_value), "+A" (*dest)
                        : "r" (add_value)
                        : "memory");
  return old_value + add_value;
}

inline void* Atomic::add_ptr(intptr_t add_value, volatile void* dest) {
  return (void *) add_ptr(add_value, (volatile intptr_t *) dest);
}

inline void Atomic::inc(volatile jint* dest) {
  add(1, dest);
}

inline void Atomic::inc_ptr(volatile void* dest) {
  add_ptr(1, dest);
}

inline void Atomic::inc_ptr(volatile intptr_t* dest) {
  add_ptr(1, dest);
}

inline void Atomic::dec(volatile jint* dest) {
  add(-1, dest);
}

inline void Atomic::dec_ptr(volatile void* dest) {
  add_ptr(-1, dest);
}

inline void Atomic::dec_ptr(volatile intptr_t* dest) {
  add_ptr(-1, dest);
}

// Atomic::xchg using AMO (amoswap.w.aqrl)
inline jint Atomic::xchg(jint exchange_value, volatile jint* dest) {
  jint old_value;
  __asm__ __volatile__ ("amoswap.w.aqrl %0, %2, %1"
                        : "=r" (old_value), "+A" (*dest)
                        : "r" (exchange_value)
                        : "memory");
  return old_value;
}

// Atomic::xchg_ptr using AMO (amoswap.d.aqrl)
inline intptr_t Atomic::xchg_ptr(intptr_t exchange_value, volatile intptr_t* dest) {
  intptr_t old_value;
  __asm__ __volatile__ ("amoswap.d.aqrl %0, %2, %1"
                        : "=r" (old_value), "+A" (*dest)
                        : "r" (exchange_value)
                        : "memory");
  return old_value;
}

inline void* Atomic::xchg_ptr(void* exchange_value, volatile void* dest) {
  return (void *) xchg_ptr((intptr_t) exchange_value, (volatile intptr_t *) dest);
}

// Atomic::cmpxchg with full memory barrier semantics
inline jint Atomic::cmpxchg(jint exchange_value, volatile jint* dest, jint compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline jlong Atomic::cmpxchg(jlong exchange_value, volatile jlong* dest, jlong compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline intptr_t Atomic::cmpxchg_ptr(intptr_t exchange_value, volatile intptr_t* dest, intptr_t compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline void* Atomic::cmpxchg_ptr(void* exchange_value, volatile void* dest, void* compare_value) {
  return (void *) cmpxchg_ptr((intptr_t) exchange_value,
                              (volatile intptr_t*) dest,
                              (intptr_t) compare_value);
}

#endif // OS_CPU_LINUX_RISCV_VM_ATOMIC_LINUX_RISCV_INLINE_HPP
