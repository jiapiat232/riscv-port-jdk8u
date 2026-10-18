/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2015, Red Hat Inc. All rights reserved.
 * Copyright (c) 2020, 2022, Huawei Technologies Co., Ltd. All rights reserved.
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

#ifndef CPU_RISCV_VM_C1_MACROASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_C1_MACROASSEMBLER_RISCV_HPP

using MacroAssembler::build_frame;
using MacroAssembler::null_check;

// C1_MacroAssembler contains high-level macros for C1

 private:
  int _rsp_offset;    // track rsp changes
  // initialization
  void pd_init() { _rsp_offset = 0; }

 public:
  void try_allocate(
    Register obj,                      // result: pointer to object after successful allocation
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  void initialize_header(Register obj, Register klass, Register len, Register tmp1, Register tmp2);
  void initialize_body(Register obj, Register len_in_bytes, int hdr_size_in_bytes, Register tmp);

  void float_cmp(bool is_float, int unordered_result,
                 FloatRegister f0, FloatRegister f1,
                 Register result);

  // locking
  void lock_object  (Register swap, Register obj, Register disp_hdr, Register scratch, Label& slow_case);
  void unlock_object(Register swap, Register obj, Register lock, Label& slow_case);

  void initialize_object(
    Register obj,                      // result: pointer to object after successful allocation
    Register klass,                    // object klass
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if known at compile time
    Register tmp1,                     // temp register
    Register tmp2                      // temp register
  );

  // allocation of fixed-size objects
  void allocate_object(
    Register obj,                      // result: pointer to object after successful allocation
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    int      header_size,              // header size in words
    int      object_size,              // object size in words
    Register klass,                    // object klass
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  enum {
    max_array_allocation_length = 0x00FFFFFF
  };

  // allocation of arrays
  void allocate_array(
    Register obj,                      // result: pointer to array after successful allocation
    Register len,                      // array length
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    int      array_header_size,        // array header size in bytes
    int      element_size,             // element size in bytes
    Register klass,                    // object klass
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  // invalidates obj for debugging
  void invalidate_done() PRODUCT_RETURN;

#endif // CPU_RISCV_VM_C1_MACROASSEMBLER_RISCV_HPP
