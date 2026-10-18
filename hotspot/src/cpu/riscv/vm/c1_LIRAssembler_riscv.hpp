/*
 * Copyright (c) 2000, 2026, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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

#ifndef CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP

// ArrayCopyStub needs access to bailout
friend class ArrayCopyStub;

 private:

  int array_element_size(BasicType type) const;

  static Register as_reg(LIR_Opr op) {
    return op->is_double_cpu() ? op->as_register_lo() : op->as_register();
  }

  Address as_Address(LIR_Address* addr, Register tmp);

  address float_constant(float f);
  address double_constant(double d);
  address int_constant(jlong n);

  Address stack_slot_address(int index, uint shift, int adjust = 0);

  void type_profile_helper(Register mdo, ciMethodData *md,
                           ciProfileData *data, Register recv);

  void casw(Register addr, Register newval, Register cmpval);
  void caswu(Register addr, Register newval, Register cmpval);
  void casl(Register addr, Register newval, Register cmpval);

  void deoptimize_trap(CodeEmitInfo *info);

public:

  enum {
    call_stub_size = 12 * NativeInstruction::instruction_size,
    exception_handler_size = DEBUG_ONLY(1*K) NOT_DEBUG(175),
    deopt_handler_size = 7 * NativeInstruction::instruction_size
  };

  void store_parameter(Register r, int offset_from_esp_in_words);
  void store_parameter(jint c,     int offset_from_esp_in_words);
  void store_parameter(jobject c,  int offset_from_esp_in_words);

#endif // CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP
