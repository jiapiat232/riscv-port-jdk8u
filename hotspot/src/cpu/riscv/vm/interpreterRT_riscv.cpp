/*
 * Copyright (c) 1998, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/universe.inline.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/icache.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/signature.hpp"

#define __ _masm->

// Implementation of SignatureHandlerGenerator
Register InterpreterRuntime::SignatureHandlerGenerator::from() { return xlocals; }
Register InterpreterRuntime::SignatureHandlerGenerator::to()   { return sp; }
Register InterpreterRuntime::SignatureHandlerGenerator::temp() { return t0; }

void InterpreterRuntime::SignatureHandlerGenerator::pass_int() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset()));
  if (_num_reg_int_args < (unsigned int)Argument::n_int_register_parameters_c) {
    __ lw(as_Register(10 + _num_reg_int_args++), src);
  } else {
    __ lw(t0, src);
    __ sd(t0, Address(to(), _stack_offset));
    _stack_offset += wordSize;
    _num_reg_int_args++;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_long() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset() + 1));
  if (_num_reg_int_args < (unsigned int)Argument::n_int_register_parameters_c) {
    __ ld(as_Register(10 + _num_reg_int_args++), src);
  } else {
    __ ld(t0, src);
    __ sd(t0, Address(to(), _stack_offset));
    _stack_offset += wordSize;
    _num_reg_int_args++;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_float() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset()));
  if (_num_reg_fp_args < (unsigned int)Argument::n_float_register_parameters_c) {
    __ flw(as_FloatRegister(10 + _num_reg_fp_args++), src);
  } else {
    __ flw(f0, src);
    __ fsd(f0, Address(to(), _stack_offset));
    _stack_offset += wordSize;
    _num_reg_fp_args++;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_double() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset() + 1));
  if (_num_reg_fp_args < (unsigned int)Argument::n_float_register_parameters_c) {
    __ fld(as_FloatRegister(10 + _num_reg_fp_args++), src);
  } else {
    __ fld(f0, src);
    __ fsd(f0, Address(to(), _stack_offset));
    _stack_offset += wordSize;
    _num_reg_fp_args++;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_object() {
  if (_num_reg_int_args < (unsigned int)Argument::n_int_register_parameters_c) {
    Register dst = as_Register(10 + _num_reg_int_args++);
    __ addi(dst, from(), Interpreter::local_offset_in_bytes(offset()));
    __ ld(temp(), Address(dst, 0));
    Label L;
    __ bnez(temp(), L);
    __ mv(dst, zr);
    __ bind(L);
  } else {
    __ addi(t1, from(), Interpreter::local_offset_in_bytes(offset()));
    __ ld(temp(), Address(t1, 0));
    Label L;
    __ bnez(temp(), L);
    __ mv(t1, zr);
    __ bind(L);
    __ sd(t1, Address(to(), _stack_offset));
    _stack_offset += wordSize;
    _num_reg_int_args++;
  }
}

InterpreterRuntime::SignatureHandlerGenerator::SignatureHandlerGenerator(methodHandle method, CodeBuffer* buffer) : NativeSignatureIterator(method) {
  _masm = new MacroAssembler(buffer);
  _num_reg_int_args = (method->is_static() ? 1 : 0);
  _num_reg_fp_args = 0;
  _stack_offset = 0;
}

void InterpreterRuntime::SignatureHandlerGenerator::generate(uint64_t fingerprint) {
  iterate(fingerprint);
  __ ret();
  __ flush();
}
