/*
 * Copyright (c) 1997, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_RISCV_VM_ASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_ASSEMBLER_RISCV_HPP

#include "asm/register.hpp"

class Argument VALUE_OBJ_CLASS_SPEC {
 public:
  enum {
    n_int_register_parameters_c   = 8,  // x10..x17 (c_rarg0..c_rarg7)
    n_float_register_parameters_c = 8,  // f10..f17 (c_farg0..c_farg7)

    n_int_register_parameters_j   = 8,
    n_float_register_parameters_j = 8
  };
};

class Address VALUE_OBJ_CLASS_SPEC {
 private:
  Register _base;
  int32_t  _offset;

 public:
  Address() : _base(noreg), _offset(0) {}
  Address(Register base, int32_t offset = 0) : _base(base), _offset(offset) {}

  Register base()   const { return _base; }
  int32_t  offset() const { return _offset; }
};

class Assembler : public AbstractAssembler {
 public:
  Assembler(CodeBuffer* code) : AbstractAssembler(code) {}

  // Emit 32-bit instruction
  void emit_int32(int32_t x) {
    AbstractAssembler::emit_int32(x);
  }

  virtual RegisterOrConstant delayed_value_impl(intptr_t* delayed_value_addr,
                                                Register tmp,
                                                int offset) {
    ShouldNotCallThis();
    return RegisterOrConstant();
  }

  virtual void bang_stack_with_offset(int offset) {
    ShouldNotCallThis();
  }
};

#endif // CPU_RISCV_VM_ASSEMBLER_RISCV_HPP
