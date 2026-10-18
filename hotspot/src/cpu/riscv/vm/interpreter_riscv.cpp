/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates.
 * Copyright (c) 2020, 2022, Huawei Technologies Co., Ltd.
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

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "interpreter/bytecodeHistogram.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterGenerator.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "oops/arrayOop.hpp"
#include "oops/methodData.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiThreadState.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/arguments.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/timer.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/debug.hpp"

#define __ _masm->

address AbstractInterpreterGenerator::generate_slow_signature_handler() {
  address entry = __ pc();

  __ andi(esp, esp, -16);
  __ mv(c_rarg3, esp);
  // xmethod: Method*
  // xlocals: locals
  // c_rarg3: first stack arg - wordSize

  // adjust sp
  __ subi(sp, c_rarg3, 18 * wordSize);
  __ subi(sp, sp, 2 * wordSize);
  __ sd(ra, Address(sp, 0));

  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::slow_signature_handler),
             xmethod, xlocals, c_rarg3);

  // ra restore
  __ ld(ra, Address(sp, 0));
  __ addi(sp, sp, 2 * wordSize);

  // FP registers first
  __ lwu(c_rarg3, Address(sp, 9 * wordSize)); // float/double identifiers

  for (int i = 0; i < Argument::n_float_register_parameters_c; i++) {
    const FloatRegister r = g_FPArgReg[i];
    Label d, done;
    __ test_bit(t0, c_rarg3, i);
    __ bnez(t0, d);
    __ flw(r, Address(sp, (10 + i) * wordSize));
    __ j(done);
    __ bind(d);
    __ fld(r, Address(sp, (10 + i) * wordSize));
    __ bind(done);
  }

  // Load integer argument registers
  for (int i = 1; i < Argument::n_int_register_parameters_c; i++) {
    const Register rm = g_INTArgReg[i];
    __ ld(rm, Address(sp, i * wordSize));
  }

  __ addi(sp, sp, 18 * wordSize);
  __ ret();

  return entry;
}

// Various method entries
address InterpreterGenerator::generate_math_entry(AbstractInterpreter::MethodKind kind) {
  // xmethod: Method*
  // x19_sender_sp: sender sp
  // esp: args

  if (!InlineIntrinsics) return NULL;

  address entry_point = NULL;
  Register continuation = ra;

  switch (kind) {
  case Interpreter::java_lang_math_abs:
    entry_point = __ pc();
    __ fld(f10, Address(esp, 0));
    __ fabs_d(f10, f10);
    __ mv(sp, x19_sender_sp);
    break;
  case Interpreter::java_lang_math_sqrt:
    entry_point = __ pc();
    __ fld(f10, Address(esp, 0));
    __ fsqrt_d(f10, f10);
    __ mv(sp, x19_sender_sp);
    break;
  case Interpreter::java_lang_math_sin :
  case Interpreter::java_lang_math_cos :
  case Interpreter::java_lang_math_tan :
  case Interpreter::java_lang_math_log :
  case Interpreter::java_lang_math_log10 :
  case Interpreter::java_lang_math_exp :
    entry_point = __ pc();
    __ fld(f10, Address(esp, 0));
    __ mv(sp, x19_sender_sp);
    __ mv(x9, ra); // Callee-saved x9 preserves return address
    continuation = x9;
    generate_transcendental_entry(kind, 1);
    break;
  case Interpreter::java_lang_math_pow :
    entry_point = __ pc();
    __ mv(x9, ra);
    continuation = x9;
    __ fld(f10, Address(esp, 2 * Interpreter::stackElementSize));
    __ fld(f11, Address(esp, 0));
    __ mv(sp, x19_sender_sp);
    generate_transcendental_entry(kind, 2);
    break;
  default:
    break;
  }

  if (entry_point != NULL) {
    __ jalr(zr, continuation, 0);
  }

  return entry_point;
}

void InterpreterGenerator::generate_transcendental_entry(AbstractInterpreter::MethodKind kind, int fpargs) {
  address fn = NULL;
  switch (kind) {
  case Interpreter::java_lang_math_sin :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dsin);
    break;
  case Interpreter::java_lang_math_cos :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dcos);
    break;
  case Interpreter::java_lang_math_tan :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dtan);
    break;
  case Interpreter::java_lang_math_log :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dlog);
    break;
  case Interpreter::java_lang_math_log10 :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dlog10);
    break;
  case Interpreter::java_lang_math_exp :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dexp);
    break;
  case Interpreter::java_lang_math_pow :
    fn = CAST_FROM_FN_PTR(address, SharedRuntime::dpow);
    break;
  default:
    ShouldNotReachHere();
  }
  __ movptr(t0, (intptr_t)fn);
  __ jalr(ra, t0, 0);
}

// Abstract method entry: throw AbstractMethodError
address InterpreterGenerator::generate_abstract_entry(void) {
  address entry_point = __ pc();

  __ empty_expression_stack();
  __ restore_bcp();
  __ restore_locals();

  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                             InterpreterRuntime::throw_AbstractMethodError));
  __ should_not_reach_here();

  return entry_point;
}

// Empty method entry: fast return
address InterpreterGenerator::generate_empty_entry(void) {
  if (!UseFastEmptyMethods) {
    return NULL;
  }

  address entry_point = __ pc();

  // If safepoint check is needed, generate full normal interpreter entry
  Label slow_path;
  {
    address poll_address = os::get_polling_page();
    __ movptr(t0, (intptr_t)poll_address);
    __ lw(t1, Address(t0, 0)); // Global polling page read check
  }

  __ mv(sp, x19_sender_sp);
  __ ret();

  __ bind(slow_path);
  (void) generate_normal_entry(false);
  return entry_point;
}

void Deoptimization::unwind_callee_save_values(frame* f, vframeArray* vframe_array) {
  assert(f->is_interpreted_frame(), "must be interpreted");
}
