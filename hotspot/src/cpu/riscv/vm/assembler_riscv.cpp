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

#include "precompiled.hpp"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "runtime/os.hpp"

const Register g_INTArgReg[Argument::n_int_register_parameters_c] = {
  c_rarg0, c_rarg1, c_rarg2, c_rarg3, c_rarg4, c_rarg5, c_rarg6, c_rarg7
};

const FloatRegister g_FPArgReg[Argument::n_float_register_parameters_c] = {
  c_farg0, c_farg1, c_farg2, c_farg3, c_farg4, c_farg5, c_farg6, c_farg7
};

int AbstractAssembler::code_fill_byte() {
  return 0; // nop on riscv is addi zr, zr, 0 (0x00000013) but code fill byte is 0
}

void Assembler::bang_stack_with_offset(int offset) {
  Unimplemented();
}

static RelocationHolder address_relocation(address target, relocInfo::relocType rtype) {
  switch (rtype) {
    case relocInfo::oop_type:
    case relocInfo::metadata_type:
      return RelocationHolder::none;
    case relocInfo::external_word_type:
      return external_word_Relocation::spec(target);
    case relocInfo::internal_word_type:
      return internal_word_Relocation::spec(target);
    case relocInfo::opt_virtual_call_type:
      return opt_virtual_call_Relocation::spec();
    case relocInfo::static_call_type:
      return static_call_Relocation::spec();
    case relocInfo::runtime_call_type:
      return runtime_call_Relocation::spec();
    case relocInfo::poll_type:
    case relocInfo::poll_return_type:
      return Relocation::spec_simple(rtype);
    case relocInfo::none:
      return RelocationHolder::none;
    default:
      ShouldNotReachHere();
      return RelocationHolder::none;
  }
}

Address::Address(address target, relocInfo::relocType rtype) :
  _mode(literal),
  _base(noreg),
  _offset(0),
  _target(target),
  _rspec(address_relocation(target, rtype))
{}
