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
#ifdef ASSERT
  {
    Label L;
    ld(t0, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
    beqz(t0, L);
    stop("InterpreterMacroAssembler::call_VM_leaf_base: last_sp != NULL");
    bind(L);
  }
#endif /* ASSERT */
  MacroAssembler::call_VM_leaf_base(entry_point, number_of_arguments);
}

void InterpreterMacroAssembler::call_VM_base(Register oop_result,
                                             Register java_thread,
                                             Register last_java_sp,
                                             address  entry_point,
                                             int number_of_arguments,
                                             bool check_exceptions) {
  // interpreter specific: bcp must be saved/restored since it may change due to GC
  save_bcp();
#ifdef ASSERT
  {
    Label L;
    ld(t0, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
    beqz(t0, L);
    stop("InterpreterMacroAssembler::call_VM_base: last_sp != NULL");
    bind(L);
  }
#endif /* ASSERT */
  // super call
  MacroAssembler::call_VM_base(oop_result, java_thread, last_java_sp, entry_point, number_of_arguments, check_exceptions);
  // interpreter specific
  restore_bcp();
  restore_locals();
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
  mv(x19_sender_sp, sp);
  sd(esp, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
}

void InterpreterMacroAssembler::jump_from_interpreted(Register method, Register temp) {
  prepare_to_jump_from_interpreted();
  ld(temp, Address(method, Method::from_interpreted_offset()));
  jr(temp);
}

void InterpreterMacroAssembler::get_dispatch() {
  movptr(xdispatch, (intptr_t)Interpreter::dispatch_table());
}

void InterpreterMacroAssembler::dispatch_base(TosState state, address* table, bool verifyoop) {
  if (table == Interpreter::dispatch_table(state)) {
    int distance = Interpreter::distance_from_dispatch_table(state);
    if (distance != 0) {
      mv(t1, distance);
      add(t1, t0, t1);
      slli(t1, t1, LogBytesPerWord);
      add(t1, xdispatch, t1);
    } else {
      slli(t1, t0, LogBytesPerWord);
      add(t1, xdispatch, t1);
    }
  } else {
    movptr(t1, (intptr_t)table);
    slli(t2, t0, LogBytesPerWord);
    add(t1, t1, t2);
  }
  ld(t1, Address(t1, 0));
  jr(t1);
}

void InterpreterMacroAssembler::dispatch_only(TosState state) {
  // dispatch_only dispatches the bytecode at xbcp without stepping xbcp.
  // In RISC-V interpreter calling convention, dispatch_base indexes the
  // dispatch table using t0 as the bytecode opcode, so t0 must be loaded here.
  lbu(t0, Address(xbcp, 0));
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
  assert(bcp_offset > 0, "bcp is still pointing to start of bytecode");
  if (index_size == sizeof(u2)) {
    load_unsigned_short(index, Address(xbcp, bcp_offset));
  } else if (index_size == sizeof(u4)) {
    assert(EnableInvokeDynamic, "giant index used only for JSR 292");
    lwu(index, Address(xbcp, bcp_offset));
    // Check if the secondary index definition is still ~x, otherwise
    // we have to change the following assembler code to calculate the
    // plain index.
    assert(ConstantPool::decode_invokedynamic_index(~123) == 123, "else change next line");
    notr(index, index);  // convert to plain index
  } else if (index_size == sizeof(u1)) {
    load_unsigned_byte(index, Address(xbcp, bcp_offset));
  } else {
    ShouldNotReachHere();
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
  load_heap_oop(result, Address(result, arrayOopDesc::base_offset_in_bytes(T_OBJECT)));
}

// Lock an object. Used in monitorenter bytecode and lock_method.
// Combines JDK 8u AArch64 biased locking flow (interp_masm_aarch64.cpp:644-729)
// with JDK 19 RISC-V fast stack-lock CAS (interp_masm_riscv.cpp:784-847).
//
// Args:
//   c_rarg1 (lock_reg): BasicObjectLock for lock
// Kills:
//   x10, c_rarg1 (x11), c_rarg2 (x12), c_rarg3 (x13), t0, t1
void InterpreterMacroAssembler::lock_object(Register lock_reg) {
  assert(lock_reg == c_rarg1, "The argument is only for looks. It must be c_rarg1");
  if (UseHeavyMonitors) {
    call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorenter), lock_reg);
  } else {
    Label done;

    const Register swap_reg = x10;
    const Register tmp      = c_rarg2;
    const Register obj_reg  = c_rarg3; // Will contain the oop

    const int obj_offset  = BasicObjectLock::obj_offset_in_bytes();
    const int lock_offset = BasicObjectLock::lock_offset_in_bytes();
    const int mark_offset = lock_offset + BasicLock::displaced_header_offset_in_bytes();

    Label slow_case;

    // Load object pointer into obj_reg (c_rarg3)
    ld(obj_reg, Address(lock_reg, obj_offset));

    if (UseBiasedLocking) {
      biased_locking_enter(lock_reg, obj_reg, swap_reg, tmp, false, done, &slow_case);
    }

    // Load (object->mark() | 1) into swap_reg
    ld(t0, Address(obj_reg, oopDesc::mark_offset_in_bytes()));
    ori(swap_reg, t0, markOopDesc::unlocked_value);

    // Save (object->mark() | 1) into BasicLock's displaced header
    sd(swap_reg, Address(lock_reg, mark_offset));

    assert(lock_offset == 0, "displaced header must be first word in BasicObjectLock");

    cmpxchg_obj_header(swap_reg, lock_reg, obj_reg, t0, done, /*fallthrough*/NULL);

    // Test if the oopMark is an obvious stack pointer, i.e.,
    //  1) (mark & 7) == 0, and
    //  2) sp <= mark < mark + os::pagesize()
    //
    // These 3 tests can be done by evaluating the following
    // expression: ((mark - sp) & (7 - os::vm_page_size())),
    // assuming both stack pointer and pagesize have their
    // least significant 3 bits clear.
    // NOTE: the oopMark is in swap_reg (x10) as the result of cmpxchg_obj_header
    sub(swap_reg, swap_reg, sp);
    li(t0, (intptr_t)(7 - os::vm_page_size()));
    andr(swap_reg, swap_reg, t0);

    // Save the test result, for recursive case, the result is zero
    sd(swap_reg, Address(lock_reg, mark_offset));

    beqz(swap_reg, done);

    bind(slow_case);

    // Call the runtime routine for slow case
    call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorenter), lock_reg);

    bind(done);
  }
}

// Unlocks an object. Used in monitorexit bytecode and remove_activation.
// NOTE: Unlike JDK 19 where biased locking is removed and monitorexit is a JRT_LEAF,
// in JDK 8u UseBiasedLocking is enabled by default (after BiasedLockingStartupDelay=4000ms)
// and ObjectSynchronizer::fast_exit asserts !object->mark()->has_bias_pattern().
// Therefore biased_locking_exit MUST intercept biased objects before slow path!
// Also, in JDK 8u InterpreterRuntime::monitorexit is IRT_ENTRY_NO_ASYNC, so slow path
// MUST use call_VM (not call_VM_leaf).
void InterpreterMacroAssembler::unlock_object(Register lock_reg) {
  assert(lock_reg == c_rarg1, "The argument is only for looks. It must be c_rarg1");
  if (UseHeavyMonitors) {
    call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorexit), lock_reg);
  } else {
    Label done;

    const Register swap_reg   = x10;
    const Register header_reg = c_rarg2; // Will contain the old oopMark
    const Register obj_reg    = c_rarg3; // Will contain the oop

    save_bcp(); // Save in case of exception

    // Convert from BasicObjectLock structure to object and BasicLock structure
    la(swap_reg, Address(lock_reg, BasicObjectLock::lock_offset_in_bytes()));

    // Load oop into obj_reg (c_rarg3)
    ld(obj_reg, Address(lock_reg, BasicObjectLock::obj_offset_in_bytes()));

    // Free entry
    sd(zr, Address(lock_reg, BasicObjectLock::obj_offset_in_bytes()));

    if (UseBiasedLocking) {
      biased_locking_exit(obj_reg, header_reg, done);
    }

    // Load the old header from BasicLock structure
    ld(header_reg, Address(swap_reg, BasicLock::displaced_header_offset_in_bytes()));

    // Test for recursion
    beqz(header_reg, done);

    // Atomic swap back the old header
    cmpxchg_obj_header(swap_reg, header_reg, obj_reg, t0, done, /*fallthrough*/NULL);

    // Call the runtime routine for slow case.
    sd(obj_reg, Address(lock_reg, BasicObjectLock::obj_offset_in_bytes())); // restore obj
    call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::monitorexit), lock_reg);

    bind(done);

    restore_bcp();
  }
}

void InterpreterMacroAssembler::remove_activation(TosState state,
                                                  bool throw_monitor_exception,
                                                  bool install_monitor_exception,
                                                  bool notify_jvmdi) {
  // Note: Registers x13 may be in use for the result check if synchronized method
  Label unlocked, unlock, no_unlock;

  // get the value of _do_not_unlock_if_synchronized into x13
  const Address do_not_unlock_if_synchronized(xthread,
    in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
  lbu(x13, do_not_unlock_if_synchronized);
  sb(zr, do_not_unlock_if_synchronized); // reset the flag

  // get method access flags
  ld(x11, Address(fp, frame::interpreter_frame_method_offset * wordSize));
  lwu(x12, Address(x11, Method::access_flags_offset()));
  andi(t0, x12, JVM_ACC_SYNCHRONIZED);
  beqz(t0, unlocked);

  // Don't unlock anything if the _do_not_unlock_if_synchronized flag is set
  bnez(x13, no_unlock);

  // unlock monitor
  push(state); // save result to Java expression stack esp

  // BasicObjectLock will be first in list, since this is a synchronized method.
  // However, need to check that the object has not been unlocked by an explicit monitorexit.
  const Address monitor(fp, frame::interpreter_frame_initial_sp_offset * wordSize - (int)sizeof(BasicObjectLock));
  la(c_rarg1, monitor); // address of first monitor

  ld(x10, Address(c_rarg1, BasicObjectLock::obj_offset_in_bytes()));
  bnez(x10, unlock);

  pop(state);
  if (throw_monitor_exception) {
    // Entry already unlocked, need to throw exception
    call_VM(noreg, CAST_FROM_FN_PTR(address,
                   InterpreterRuntime::throw_illegal_monitor_state_exception));
    should_not_reach_here();
  } else {
    // Monitor already unlocked during a stack unroll. If requested,
    // install an illegal_monitor_state_exception. Continue with stack unrolling.
    if (install_monitor_exception) {
      call_VM(noreg, CAST_FROM_FN_PTR(address,
                     InterpreterRuntime::new_illegal_monitor_state_exception));
    }
    j(unlocked);
  }

  bind(unlock);
  unlock_object(c_rarg1);
  pop(state);

  // Check that for block-structured locking (i.e., that all locked objects have been unlocked)
  bind(unlocked);

  // Check that all monitors are unlocked
  {
    Label loop, exception, entry, restart;
    const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;
    const Address monitor_block_top(
      fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
    const Address monitor_block_bot(
      fp, frame::interpreter_frame_initial_sp_offset * wordSize);

    bind(restart);
    // c_rarg1 points to current entry, starting with top-most entry
    ld(c_rarg1, monitor_block_top);
    la(x9, monitor_block_bot); // points to word before bottom of monitor block

    j(entry);

    // Entry already locked, need to throw exception
    bind(exception);

    if (throw_monitor_exception) {
      // Throw exception
      MacroAssembler::call_VM(noreg,
                              CAST_FROM_FN_PTR(address, InterpreterRuntime::
                                               throw_illegal_monitor_state_exception));
      should_not_reach_here();
    } else {
      // Stack unrolling. Unlock object and install illegal_monitor_exception.
      push(state);
      unlock_object(c_rarg1);
      pop(state);

      if (install_monitor_exception) {
        call_VM(noreg, CAST_FROM_FN_PTR(address,
                                        InterpreterRuntime::
                                        new_illegal_monitor_state_exception));
      }

      j(restart);
    }

    bind(loop);
    // check if current entry is used
    ld(t0, Address(c_rarg1, BasicObjectLock::obj_offset_in_bytes()));
    bnez(t0, exception);

    addi(c_rarg1, c_rarg1, entry_size); // advance to next entry
    bind(entry);
    bne(c_rarg1, x9, loop); // check if bottom reached
  }

  bind(no_unlock);

  // jvmti support
  notify_method_exit(state, notify_jvmdi ? NotifyJVMTI : SkipNotifyJVMTI);

  // remove activation
  ld(esp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
  leave();
  andi(sp, esp, -16);
}

void InterpreterMacroAssembler::update_mdp_for_ret(Register return_bci) {
  // Profiling support for ret bytecode
}

void InterpreterMacroAssembler::set_method_data_pointer_for_bcp() {
  assert(ProfileInterpreter, "must be profiling interpreter");
  Label set_mdp;
  subi(sp, sp, 2 * wordSize);
  sd(x10, Address(sp, 0));
  sd(x11, Address(sp, wordSize));

  ld(x10, Address(xmethod, in_bytes(Method::method_data_offset())));
  beqz(x10, set_mdp);
  call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::bcp_to_di), xmethod, xbcp);
  ld(x11, Address(xmethod, in_bytes(Method::method_data_offset())));
  addi(x11, x11, in_bytes(MethodData::data_offset()));
  add(x10, x11, x10);
  sd(x10, Address(fp, frame::interpreter_frame_mdx_offset * wordSize));

  bind(set_mdp);
  ld(x10, Address(sp, 0));
  ld(x11, Address(sp, wordSize));
  addi(sp, sp, 2 * wordSize);
}

void InterpreterMacroAssembler::test_method_data_pointer(Register mdp, Label& zero_continue) {
  assert(ProfileInterpreter, "must be profiling interpreter");
  ld(mdp, Address(fp, frame::interpreter_frame_mdx_offset * wordSize));
  beqz(mdp, zero_continue);
}

void InterpreterMacroAssembler::increment_mask_and_jump(Address counter_addr,
                                                        int increment, Address mask,
                                                        Register tmp1, Register tmp2,
                                                        bool preloaded, Label* where) {
  Label done;
  if (!preloaded) {
    lwu(tmp1, counter_addr);
  }
  addi(tmp1, tmp1, increment);
  sw(tmp1, counter_addr);
  lwu(tmp2, mask);
  andr(tmp1, tmp1, tmp2);
  bnez(tmp1, done);
  j(*where);
  bind(done);
}

void InterpreterMacroAssembler::increment_mask_and_jump(Address counter_addr,
                                                        int increment, int mask,
                                                        Register tmp1, Register tmp2,
                                                        bool preloaded, Label* where) {
  Label done;
  if (!preloaded) {
    lwu(tmp1, counter_addr);
  }
  addi(tmp1, tmp1, increment);
  sw(tmp1, counter_addr);
  li(tmp2, mask);
  andr(tmp1, tmp1, tmp2);
  bnez(tmp1, done);
  j(*where);
  bind(done);
}

void InterpreterMacroAssembler::notify_method_entry() {
  if (JvmtiExport::can_post_interpreter_events()) {
    Label L;
    lwu(x13, Address(xthread, JavaThread::interp_only_mode_offset()));
    beqz(x13, L);
    call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::post_method_entry));
    bind(L);
  }
  if (DTraceMethodProbes) {
    get_method(c_rarg1);
    call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_entry), xthread, c_rarg1);
  }
}

void InterpreterMacroAssembler::notify_method_exit(TosState state, NotifyMethodExitMode mode) {
  if (JvmtiExport::can_post_interpreter_events()) {
    Label L;
    lwu(x13, Address(xthread, JavaThread::interp_only_mode_offset()));
    beqz(x13, L);
    if (mode == NotifyJVMTI) {
      push(state);
      call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::post_method_exit));
      pop(state);
    }
    bind(L);
  }
  if (DTraceMethodProbes) {
    push(state);
    get_method(c_rarg1);
    call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_exit), xthread, c_rarg1);
    pop(state);
  }
}

void InterpreterMacroAssembler::narrow(Register result) {
  // Get method->_constMethod->_result_type
  ld(t0, Address(fp, frame::interpreter_frame_method_offset * wordSize));
  ld(t0, Address(t0, Method::const_offset()));
  lbu(t0, Address(t0, ConstMethod::result_type_offset()));

  Label done, notBool, notByte, notChar;

  // common case first: T_INT
  subi(t1, t0, T_INT);
  beqz(t1, done);

  // T_BOOLEAN
  subi(t1, t0, T_BOOLEAN);
  bnez(t1, notBool);
  andi(result, result, 0x1);
  j(done);

  bind(notBool);
  // T_BYTE
  subi(t1, t0, T_BYTE);
  bnez(t1, notByte);
  slli(result, result, 56);
  srai(result, result, 56);
  j(done);

  bind(notByte);
  // T_CHAR
  subi(t1, t0, T_CHAR);
  bnez(t1, notChar);
  slli(result, result, 48);
  srli(result, result, 48);
  j(done);

  bind(notChar);
  // T_SHORT: sign-extend short
  slli(result, result, 48);
  srai(result, result, 48);

  bind(done);
}

void InterpreterMacroAssembler::verify_frame_setup() {
#ifdef ASSERT
  Label L;
  const Address monitor_block_top(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  ld(t0, monitor_block_top);
  beq(esp, t0, L);
  stop("broken stack frame setup in interpreter");
  bind(L);
#endif
}

void InterpreterMacroAssembler::verify_field_offset(Register reg) {
#ifdef ASSERT
  Label L;
  mv(t0, (int64_t)instanceOopDesc::base_offset_in_bytes());
  bge(reg, t0, L);
  stop("bad field offset");
  bind(L);
#endif
}

#endif // !CC_INTERP
