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
  // hdr     : must be x10, contents destroyed
  // obj     : must point to the object to lock, contents preserved
  // disp_hdr: must point to the displaced header location, contents preserved
  // scratch : scratch register, contents destroyed
  // returns code offset at which to add null check debug information
  int lock_object  (Register swap, Register obj, Register disp_hdr, Register scratch, Label& slow_case);

  // unlocking
  // hdr     : contents destroyed
  // obj     : must point to the object to lock, contents preserved
  // disp_hdr: must be x10 & must point to the displaced header location, contents destroyed
  void unlock_object(Register swap, Register obj, Register lock, Label& slow_case);

  void initialize_object(
    Register obj,                      // result: pointer to object after successful allocation
    Register klass,                    // object klass
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    bool     is_tlab_allocated = false // the object was allocated in a TLAB; relevant for the implementation of ZeroTLAB
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
    int      array_header_size,        // array header size in words
    int      element_size,             // element scale factor
    Register klass,                    // object klass
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  int  rsp_offset() const { return _rsp_offset; }
  void set_rsp_offset(int n) { _rsp_offset = n; }

  void invalidate_registers(bool inv_x10, bool inv_x9, bool inv_x12, bool inv_x13, bool inv_x14, bool inv_x15) PRODUCT_RETURN;
  void invalidate_done() PRODUCT_RETURN {}

  // This platform only uses signal-based null checks. The Label is not needed.
  void null_check(Register r, Label *Lnull = NULL) { MacroAssembler::null_check(r); }

  void load_parameter(int offset_in_words, Register reg);

  void inline_cache_check(Register receiver, Register iCache, Label &L);

  static const int c1_double_branch_mask = 1 << 3; // depend on c1_float_cond_branch
  void c1_cmp_branch(int cmpFlag, Register op1, Register op2, Label& label, BasicType type, bool is_far);
  void c1_float_cmp_branch(int cmpFlag, FloatRegister op1, FloatRegister op2, Label& label,
                           bool is_far, bool is_unordered = false);

#endif // CPU_RISCV_VM_C1_MACROASSEMBLER_RISCV_HPP
