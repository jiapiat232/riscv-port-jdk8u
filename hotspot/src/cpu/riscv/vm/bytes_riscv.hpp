/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 1997, 2023, Oracle and/or its affiliates.
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

#ifndef CPU_RISCV_VM_BYTES_RISCV_HPP
#define CPU_RISCV_VM_BYTES_RISCV_HPP

#include "memory/allocation.hpp"

class Bytes: AllStatic {
 public:
  // Returns true if the byte ordering used by Java is different from the native byte ordering
  // of the underlying machine. RISC-V standard is little-endian.
  static inline bool is_Java_byte_ordering_different() { return true; }

  // Efficient reading and writing of unaligned unsigned data in platform-specific byte ordering
  static inline u2   get_native_u2(address p)         { return *(u2*)p; }
  static inline u4   get_native_u4(address p)         { return *(u4*)p; }
  static inline u8   get_native_u8(address p)         { return *(u8*)p; }

  static inline void put_native_u2(address p, u2 x)   { *(u2*)p = x; }
  static inline void put_native_u4(address p, u4 x)   { *(u4*)p = x; }
  static inline void put_native_u8(address p, u8 x)   { *(u8*)p = x; }

  // Efficient reading and writing of unaligned unsigned data in Java byte ordering (big-endian)
  static inline u2   get_Java_u2(address p)           { return swap_u2(get_native_u2(p)); }
  static inline u4   get_Java_u4(address p)           { return swap_u4(get_native_u4(p)); }
  static inline u8   get_Java_u8(address p)           { return swap_u8(get_native_u8(p)); }

  static inline void put_Java_u2(address p, u2 x)     { put_native_u2(p, swap_u2(x)); }
  static inline void put_Java_u4(address p, u4 x)     { put_native_u4(p, swap_u4(x)); }
  static inline void put_Java_u8(address p, u8 x)     { put_native_u8(p, swap_u8(x)); }

  // Efficient swapping of byte ordering
  static inline u2   swap_u2(u2 x);
  static inline u4   swap_u4(u4 x);
  static inline u8   swap_u8(u8 x);
};

#ifdef TARGET_OS_ARCH_linux_riscv
# include "bytes_linux_riscv.inline.hpp"
#endif

#endif // CPU_RISCV_VM_BYTES_RISCV_HPP
