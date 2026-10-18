/*
 * Copyright (c) 2003, 2026, Oracle and/or its affiliates. All rights reserved.
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
#include "interp_masm_riscv.hpp"
#include "asm/macroAssembler.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "memory/allocation.inline.hpp"
#include "oops/arrayOop.hpp"
#include "oops/method.hpp"
#include "oops/methodData.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiThreadState.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/thread.inline.hpp"

#ifndef CC_INTERP

void InterpreterMacroAssembler::call_VM_leaf_base(address entry_point, int number_of_arguments) {
  MacroAssembler::call(entry_point);
}

void InterpreterMacroAssembler::call_VM_base(Register oop_result,
                                             Register java_thread,
                                             Register last_java_sp,
                                             address  entry_point,
                                             int number_of_arguments,
                                             bool check_exceptions) {
  set_last_Java_frame(last_java_sp, fp, NULL, t0);
  MacroAssembler::call_VM(oop_result, entry_point, check_exceptions);
  reset_last_Java_frame(false);
}

void InterpreterMacroAssembler::check_and_handle_popframe(Register java_thread) {
  if (JvmtiExport::can_pop_frame()) {
    Label L;
    lwu(t0, Address(java_thread, JavaThread::popframe_condition_offset()));
    beqz(t0, L);
    stop("popframe requested");
    bind(L);
  }
}

void InterpreterMacroAssembler::check_and_handle_earlyret(Register java_thread) {
  if (JvmtiExport::can_force_early_return()) {
    Label L;
    lwu(t0, Address(java_thread, JavaThread::jvmti_thread_state_offset()));
    beqz(t0, L);
    bind(L);
  }
}

void InterpreterMacroAssembler::load_earlyret_value(TosState state) {
  Register thread = xthread;
  ld(t0, Address(thread, JavaThread::jvmti_thread_state_offset()));
  int off = in_bytes(JvmtiThreadState::earlyret_value_offset());
  switch (state) {
    case atos: ld(x10, Address(t0, off)); break;
    case ltos: ld(x10, Address(t0, off)); break;
    case btos:
    case ztos:
    case ctos:
    case stos:
    case itos: lwu(x10, Address(t0, off)); break;
    case ftos: flw(f10, Address(t0, off)); break;
    case dtos: fld(f10, Address(t0, off)); break;
    case vtos: break;
    default  : ShouldNotReachHere();
  }
}

// Expression stack operations
void InterpreterMacroAssembler::push_ptr(Register r) {
  addi(esp, esp, -wordSize);
  sd(r, Address(esp, 0));
}

void InterpreterMacroAssembler::push_i(Register r) {
  addi(esp, esp, -wordSize);
  sd(r, Address(esp, 0));
}

void InterpreterMacroAssembler::push_l(Register r) {
  addi(esp, esp, 2 * -wordSize);
  sd(r, Address(esp, 0));
}

void InterpreterMacroAssembler::push_f(FloatRegister r) {
  addi(esp, esp, -wordSize);
  fsd(r, Address(esp, 0));
}

void InterpreterMacroAssembler::push_d(FloatRegister r) {
  addi(esp, esp, 2 * -wordSize);
  fsd(r, Address(esp, 0));
}

void InterpreterMacroAssembler::pop_ptr(Register r) {
  ld(r, Address(esp, 0));
  addi(esp, esp, wordSize);
}

void InterpreterMacroAssembler::pop_i(Register r) {
  ld(r, Address(esp, 0));
  addi(esp, esp, wordSize);
}

void InterpreterMacroAssembler::pop_l(Register r) {
  ld(r, Address(esp, 0));
  addi(esp, esp, 2 * wordSize);
}

void InterpreterMacroAssembler::pop_f(FloatRegister r) {
  fld(r, Address(esp, 0));
  addi(esp, esp, wordSize);
}

void InterpreterMacroAssembler::pop_d(FloatRegister r) {
  fld(r, Address(esp, 0));
  addi(esp, esp, 2 * wordSize);
}

void InterpreterMacroAssembler::pop(TosState state) {
  switch (state) {
  case atos: pop_ptr(); break;
  case btos:
  case ztos:
  case ctos:
  case stos:
  case itos: pop_i();   break;
  case ltos: pop_l();   break;
  case ftos: pop_f();   break;
  case dtos: pop_d();   break;
  case vtos: break;
  default:   ShouldNotReachHere();
  }
}

void InterpreterMacroAssembler::push(TosState state) {
  switch (state) {
  case atos: push_ptr(); break;
  case btos:
  case ztos:
  case ctos:
  case stos:
  case itos: push_i();   break;
  case ltos: push_l();   break;
  case ftos: push_f();   break;
  case dtos: push_d();   break;
  case vtos: break;
  default:   ShouldNotReachHere();
  }
}

void InterpreterMacroAssembler::load_ptr(int n, Register val) {
  ld(val, Address(esp, Interpreter::expr_offset_in_bytes(n)));
}

void InterpreterMacroAssembler::store_ptr(int n, Register val) {
  sd(val, Address(esp, Interpreter::expr_offset_in_bytes(n)));
}

void InterpreterMacroAssembler::prepare_to_jump_from_interpreted() {
  mv(t0, sp);
  sd(esp, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
}

void InterpreterMacroAssembler::jump_from_interpreted(Register method, Register temp) {
  prepare_to_jump_from_interpreted();
  ld(temp, Address(method, Method::from_interpreted_offset()));
  jr(temp);
}

void InterpreterMacroAssembler::dispatch_base(TosState state, address* table, bool verifyoop) {
  movptr(t1, (intptr_t)table);
  slli(t2, t0, LogBytesPerWord);
  add(t1, t1, t2);
  ld(t1, Address(t1, 0));
  jr(t1);
}

void InterpreterMacroAssembler::dispatch_only(TosState state) {
  dispatch_base(state, Interpreter::dispatch_table(state));
}

void InterpreterMacroAssembler::dispatch_only_normal(TosState state) {
  dispatch_base(state, Interpreter::normal_table(state));
}

void InterpreterMacroAssembler::dispatch_only_noverify(TosState state) {
  dispatch_base(state, Interpreter::normal_table(state), false);
}

void InterpreterMacroAssembler::dispatch_next(TosState state, int step) {
  if (step != 0) {
    addi(xbcp, xbcp, step);
  }
  lbu(t0, Address(xbcp, 0));
  dispatch_base(state, Interpreter::dispatch_table(state));
}

void InterpreterMacroAssembler::dispatch_via(TosState state, address* table) {
  lbu(t0, Address(xbcp, 0));
  dispatch_base(state, table);
}

void InterpreterMacroAssembler::dispatch_epilog(TosState state, int step) {
  dispatch_next(state, step);
}

void InterpreterMacroAssembler::get_unsigned_2_byte_index_at_bcp(Register reg, int bcp_offset) {
  lbu(t0, Address(xbcp, bcp_offset));
  lbu(reg, Address(xbcp, bcp_offset + 1));
  slli(t0, t0, 8);
  orr(reg, reg, t0);
}

void InterpreterMacroAssembler::get_cache_index_at_bcp(Register index, int bcp_offset, size_t index_size) {
  if (index_size == sizeof(u2)) {
    get_unsigned_2_byte_index_at_bcp(index, bcp_offset);
  } else {
    assert(index_size == sizeof(u1), "size must be 1 or 2");
    lbu(index, Address(xbcp, bcp_offset));
  }
}

void InterpreterMacroAssembler::get_cache_and_index_at_bcp(Register cache, Register index, int bcp_offset, size_t index_size) {
  get_cache_index_at_bcp(index, bcp_offset, index_size);
  slli(cache, index, 5); // 32 bytes per ConstantPoolCacheEntry
  add(cache, xcpool, cache);
}

void InterpreterMacroAssembler::get_cache_and_index_and_bytecode_at_bcp(Register cache, Register index, Register bytecode, int byte_no, int bcp_offset, size_t index_size) {
  get_cache_and_index_at_bcp(cache, index, bcp_offset, index_size);
  lwu(bytecode, Address(cache, ConstantPoolCache::base_offset() + ConstantPoolCacheEntry::indices_offset()));
  const int shift_count = (1 + byte_no) * BitsPerByte;
  srli(bytecode, bytecode, shift_count);
  andi(bytecode, bytecode, 0xff);
}

void InterpreterMacroAssembler::get_cache_entry_pointer_at_bcp(Register cache, Register tmp, int bcp_offset, size_t index_size) {
  get_cache_index_at_bcp(tmp, bcp_offset, index_size);
  ld(cache, Address(fp, frame::interpreter_frame_cache_offset * wordSize));
  addi(cache, cache, in_bytes(ConstantPoolCache::base_offset()));
  slli(tmp, tmp, 5); // 32 bytes per ConstantPoolCacheEntry
  add(cache, cache, tmp);
}

void InterpreterMacroAssembler::get_method_counters(Register method, Register mcs, Label& skip) {
  Label has_counters;
  ld(mcs, Address(method, Method::method_counters_offset()));
  bnez(mcs, has_counters);
  call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::build_method_counters), method);
  ld(mcs, Address(method, Method::method_counters_offset()));
  beqz(mcs, skip);
  bind(has_counters);
}

void InterpreterMacroAssembler::load_resolved_reference_at_index(Register result, Register index) {
  get_constant_pool(result);
  ld(result, Address(result, ConstantPool::resolved_references_offset_in_bytes()));
  ld(result, Address(result, 0));
  slli(t0, index, LogBytesPerHeapOop);
  add(result, result, t0);
  if (UseCompressedOops) {
    lwu(result, Address(result, arrayOopDesc::base_offset_in_bytes(T_OBJECT)));
    slli(result, result, LogMinObjAlignmentInBytes);
    add(result, xheapbase, result);
  } else {
    ld(result, Address(result, arrayOopDesc::base_offset_in_bytes(T_OBJECT)));
  }
}

void InterpreterMacroAssembler::lock_object(Register lock_reg) {
  call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorenter), lock_reg);
}

void InterpreterMacroAssembler::unlock_object(Register lock_reg) {
  call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorexit), lock_reg);
}

void InterpreterMacroAssembler::remove_activation(TosState state,
                                                  bool throw_monitor_exception,
                                                  bool install_monitor_exception,
                                                  bool notify_jvmdi) {
  ld(esp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
  leave();
  andi(sp, esp, -16);
}

void InterpreterMacroAssembler::update_mdp_for_ret(Register return_bci) {
  // Profiling support for ret bytecode
}

#endif // !CC_INTERP
