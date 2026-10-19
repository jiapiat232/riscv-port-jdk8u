/*
 * Copyright (c) 2003, 2017, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, Red Hat Inc. All rights reserved.
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
#include "asm/macroAssembler.hpp"
#include "interpreter/bytecodeHistogram.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterGenerator.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "interpreter/bytecodeTracer.hpp"
#include "oops/arrayOop.hpp"
#include "oops/methodData.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiThreadState.hpp"
#include "runtime/arguments.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/timer.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/debug.hpp"
#include <sys/types.h>

#ifndef PRODUCT
#include "oops/method.hpp"
#endif // !PRODUCT

#define __ _masm->

#ifndef CC_INTERP

//-----------------------------------------------------------------------------

address TemplateInterpreterGenerator::generate_StackOverflowError_handler() {
  address entry = __ pc();

#ifdef ASSERT
  {
    Label L;
    __ ld(t0, Address(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize));
    __ bge(t0, sp, L);
    __ stop("interpreter frame not set up");
    __ bind(L);
  }
#endif // ASSERT

  __ restore_bcp();
  __ empty_expression_stack();
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_StackOverflowError));
  return entry;
}

address TemplateInterpreterGenerator::generate_ArrayIndexOutOfBounds_handler(
        const char* name) {
  address entry = __ pc();
  __ empty_expression_stack();
  __ zext(c_rarg2, x11, 32);
  __ movptr(c_rarg1, (intptr_t)name);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::
                              throw_ArrayIndexOutOfBoundsException),
             c_rarg1, c_rarg2);
  return entry;
}

address TemplateInterpreterGenerator::generate_ClassCastException_handler() {
  address entry = __ pc();

  // object is at TOS
  __ pop_ptr(c_rarg1);
  __ empty_expression_stack();

  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::throw_ClassCastException),
             c_rarg1);
  return entry;
}

address TemplateInterpreterGenerator::generate_exception_handler_common(
        const char* name, const char* message, bool pass_oop) {
  assert(!pass_oop || message == NULL, "either oop or message but not both");
  address entry = __ pc();
  if (pass_oop) {
    __ pop_ptr(c_rarg2);
  }
  __ empty_expression_stack();
  __ movptr(c_rarg1, (intptr_t)name);
  if (pass_oop) {
    __ call_VM(x10, CAST_FROM_FN_PTR(address,
                                     InterpreterRuntime::create_klass_exception),
               c_rarg1, c_rarg2);
  } else {
    if (message != NULL) {
      __ movptr(c_rarg2, (intptr_t)message);
    } else {
      __ mv(c_rarg2, zr);
    }
    __ call_VM(x10,
               CAST_FROM_FN_PTR(address, InterpreterRuntime::create_exception),
               c_rarg1, c_rarg2);
  }
  // throw exception
  __ far_jump(RuntimeAddress(Interpreter::throw_exception_entry()));
  return entry;
}

address TemplateInterpreterGenerator::generate_continuation_for(TosState state) {
  address entry = __ pc();
  __ get_dispatch();
  __ dispatch_next(state);
  return entry;
}

address TemplateInterpreterGenerator::generate_return_entry_for(TosState state, int step, size_t index_size) {
  address entry = __ pc();

  // Restore stack bottom in case i2c adjusted stack
  __ ld(esp, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  // and NULL it as marker that esp is now tos until next java call
  __ sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(xmethod);

  // Pop N words from the stack
  __ get_cache_and_index_at_bcp(x11, x12, 1, index_size);
  __ ld(x11, Address(x11, in_bytes(ConstantPoolCache::base_offset() + ConstantPoolCacheEntry::flags_offset())));
  __ andi(x11, x11, ConstantPoolCacheEntry::parameter_size_mask);
  __ slli(x11, x11, Interpreter::logStackElementSize);
  __ add(esp, esp, x11);

  // Restore machine SP
  __ ld(t0, Address(xmethod, Method::const_offset()));
  __ lhu(t0, Address(t0, ConstMethod::max_stack_offset()));
  __ addi(t0, t0, frame::interpreter_frame_monitor_size() + 2);
  __ slli(t0, t0, Interpreter::logStackElementSize);
  __ ld(t1, Address(fp, frame::interpreter_frame_initial_sp_offset * wordSize));
  __ sub(t0, t1, t0);
  __ andi(sp, t0, -16);

  __ get_dispatch();
  __ dispatch_next(state, step);

  return entry;
}

address TemplateInterpreterGenerator::generate_deopt_entry_for(TosState state,
                                                               int step) {
  address entry = __ pc();
  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(xmethod);
  __ get_dispatch();

  // Calculate stack limit
  __ ld(t0, Address(xmethod, Method::const_offset()));
  __ lhu(t0, Address(t0, ConstMethod::max_stack_offset()));
  __ addi(t0, t0, frame::interpreter_frame_monitor_size() + (EnableInvokeDynamic ? 2 : 0));
  __ slli(t0, t0, Interpreter::logStackElementSize);
  __ ld(t1, Address(fp, frame::interpreter_frame_initial_sp_offset * wordSize));
  __ sub(t0, t1, t0);
  __ andi(sp, t0, -16);

  // Restore expression stack pointer
  __ ld(esp, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  // NULL last_sp until next java call
  __ sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));

  // handle exceptions
  {
    Label L;
    __ ld(t0, Address(xthread, Thread::pending_exception_offset()));
    __ beqz(t0, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }

  __ dispatch_next(state, step);
  return entry;
}

int AbstractInterpreter::BasicType_as_index(BasicType type) {
  int i = 0;
  switch (type) {
    case T_BOOLEAN: i = 0; break;
    case T_CHAR   : i = 1; break;
    case T_BYTE   : i = 2; break;
    case T_SHORT  : i = 3; break;
    case T_INT    : i = 4; break;
    case T_LONG   : i = 5; break;
    case T_VOID   : i = 6; break;
    case T_FLOAT  : i = 7; break;
    case T_DOUBLE : i = 8; break;
    case T_OBJECT : i = 9; break;
    case T_ARRAY  : i = 9; break;
    default       : ShouldNotReachHere();
  }
  assert(0 <= i && i < AbstractInterpreter::number_of_result_handlers,
         "index out of bounds");
  return i;
}

address TemplateInterpreterGenerator::generate_result_handler_for(BasicType type) {
  address entry = __ pc();
  if (type == T_OBJECT) {
    __ ld(x10, Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize));
    __ verify_oop(x10);
  } else {
    __ cast_primitive_type(type, x10);
  }
  __ ret();
  return entry;
}

address TemplateInterpreterGenerator::generate_safept_entry_for(
        TosState state,
        address runtime_entry) {
  address entry = __ pc();
  __ push(state);
  __ call_VM(noreg, runtime_entry);
  __ membar(MacroAssembler::AnyAny);
  __ dispatch_via(vtos, Interpreter::_normal_table.table_for(vtos));
  return entry;
}

void InterpreterGenerator::generate_counter_incr(
        Label* overflow,
        Label* profile_method,
        Label* profile_method_continue) {
  Label done;
  if (TieredCompilation) {
    int increment = InvocationCounter::count_increment;
    int mask = ((1 << Tier0InvokeNotifyFreqLog) - 1) << InvocationCounter::count_shift;
    Label no_mdo;
    if (ProfileInterpreter) {
      // Are we profiling?
      __ ld(x10, Address(xmethod, Method::method_data_offset()));
      __ beqz(x10, no_mdo);
      // Increment counter in the MDO
      const Address mdo_invocation_counter(x10, in_bytes(MethodData::invocation_counter_offset()) +
                                                in_bytes(InvocationCounter::counter_offset()));
      __ increment_mask_and_jump(mdo_invocation_counter, increment, mask, t0, t1, false, overflow);
      __ j(done);
    }
    __ bind(no_mdo);
    // Increment counter in MethodCounters
    __ get_method_counters(xmethod, t1, done);
    const Address invocation_counter(t1,
                  MethodCounters::invocation_counter_offset() +
                  InvocationCounter::counter_offset());
    __ increment_mask_and_jump(invocation_counter, increment, mask, t0, x11, false, overflow);
    __ bind(done);
  } else {
    __ get_method_counters(xmethod, t1, done);
    const Address backedge_counter(t1,
                  MethodCounters::backedge_counter_offset() +
                  InvocationCounter::counter_offset());
    const Address invocation_counter(t1,
                  MethodCounters::invocation_counter_offset() +
                  InvocationCounter::counter_offset());

    if (ProfileInterpreter) {
      __ lwu(t0, Address(t1, MethodCounters::interpreter_invocation_counter_offset()));
      __ addiw(t0, t0, 1);
      __ sw(t0, Address(t1, MethodCounters::interpreter_invocation_counter_offset()));
    }
    // Update standard invocation counters
    __ lwu(x10, invocation_counter);
    __ lwu(t0, backedge_counter);

    __ addiw(x10, x10, InvocationCounter::count_increment);
    __ andi(t0, t0, InvocationCounter::count_mask_value);

    __ sw(x10, invocation_counter);
    __ addw(t0, t0, x10);                // add both counters

    if (ProfileInterpreter && profile_method != NULL) {
      __ movptr(x11, (intptr_t)&InvocationCounter::InterpreterProfileLimit);
      __ lwu(x11, Address(x11, 0));
      __ bgeu(x11, t0, *profile_method_continue);

      // if no method data exists, go to profile_method
      __ test_method_data_pointer(x11, *profile_method);
    }

    {
      __ movptr(x11, (intptr_t)&InvocationCounter::InterpreterInvocationLimit);
      __ lwu(x11, Address(x11, 0));
      __ bgeu(t0, x11, *overflow);
    }
    __ bind(done);
  }
}

void InterpreterGenerator::generate_counter_overflow(Label* do_continue) {
  __ mv(c_rarg1, zr);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::frequency_counter_overflow),
             c_rarg1);
  __ j(*do_continue);
}

void InterpreterGenerator::generate_stack_overflow_check(void) {
  const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;
  const int overhead_size =
    -(frame::interpreter_frame_initial_sp_offset * wordSize) + entry_size;
  const int page_size = (int)os::vm_page_size();

  Label after_frame_check;

  __ mv(t0, (page_size - overhead_size) / Interpreter::stackElementSize);
  __ bleu(x13, t0, after_frame_check);

  __ mv(t0, overhead_size);
  __ shift_left_add(x10, x13, t0, Interpreter::logStackElementSize);

  const Address stack_limit(xthread, JavaThread::stack_overflow_limit_offset());
  __ ld(t0, stack_limit);

#ifdef ASSERT
  Label limit_okay;
  __ bnez(t0, limit_okay);
  __ stop("stack overflow limit is zero");
  __ bind(limit_okay);
#endif

  __ add(x10, x10, t0);
  __ bgtu(sp, x10, after_frame_check);

  __ andi(sp, x19_sender_sp, -16);
  assert(StubRoutines::throw_StackOverflowError_entry() != NULL, "stub not yet generated");
  __ far_jump(RuntimeAddress(StubRoutines::throw_StackOverflowError_entry()));

  __ bind(after_frame_check);
}

void InterpreterGenerator::lock_method(void) {
  const Address access_flags(xmethod, Method::access_flags_offset());
  const Address monitor_block_top(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;

#ifdef ASSERT
  {
    Label L;
    __ lwu(x10, access_flags);
    __ andi(t0, x10, JVM_ACC_SYNCHRONIZED);
    __ bnez(t0, L);
    __ stop("method doesn't need synchronization");
    __ bind(L);
  }
#endif // ASSERT

  // get synchronization object
  {
    const int mirror_offset = in_bytes(Klass::java_mirror_offset());
    Label done;
    __ lwu(x10, access_flags);
    __ andi(t0, x10, JVM_ACC_STATIC);
    // get receiver (assume this is frequent case)
    __ ld(x10, Address(xlocals, Interpreter::local_offset_in_bytes(0)));
    __ beqz(t0, done);
    __ ld(x10, Address(xmethod, Method::const_offset()));
    __ ld(x10, Address(x10, ConstMethod::constants_offset()));
    __ ld(x10, Address(x10, ConstantPool::pool_holder_offset_in_bytes()));
    __ ld(x10, Address(x10, mirror_offset));

#ifdef ASSERT
    {
      Label L;
      __ bnez(x10, L);
      __ stop("synchronization object is NULL");
      __ bind(L);
    }
#endif // ASSERT

    __ bind(done);
  }

  // add space for monitor & lock
  __ subi(sp, sp, entry_size);
  __ subi(esp, esp, entry_size);
  __ sd(esp, monitor_block_top);
  __ sd(x10, Address(esp, BasicObjectLock::obj_offset_in_bytes()));
  __ mv(c_rarg1, esp);
  __ lock_object(c_rarg1);
}

void TemplateInterpreterGenerator::generate_fixed_frame(bool native_call) {
  // initialize fixed part of activation frame
  if (native_call) {
    __ subi(esp, sp, 12 * wordSize);
    __ mv(xbcp, zr);
    __ subi(sp, sp, 12 * wordSize);
    // add 2 zero-initialized slots for native calls
    __ sd(zr, Address(sp, 11 * wordSize));
    __ sd(zr, Address(sp, 10 * wordSize));
  } else {
    __ subi(esp, sp, 10 * wordSize);
    __ ld(t0, Address(xmethod, Method::const_offset()));
    __ addi(xbcp, t0, in_bytes(ConstMethod::codes_offset()));
    __ subi(sp, sp, 10 * wordSize);
  }

  __ sd(esp, Address(sp, 0 * wordSize)); // initial_sp
  __ sd(xbcp, Address(sp, 1 * wordSize)); // bcx
  __ sd(xlocals, Address(sp, 2 * wordSize)); // locals

  __ ld(xcpool, Address(xmethod, Method::const_offset()));
  __ ld(xcpool, Address(xcpool, ConstMethod::constants_offset()));
  __ ld(xcpool, Address(xcpool, ConstantPool::cache_offset_in_bytes()));
  __ sd(xcpool, Address(sp, 3 * wordSize)); // cache

  if (ProfileInterpreter) {
    Label method_data_continue;
    __ ld(t0, Address(xmethod, Method::method_data_offset()));
    __ beqz(t0, method_data_continue);
    __ addi(t0, t0, in_bytes(MethodData::data_offset()));
    __ bind(method_data_continue);
    __ sd(t0, Address(sp, 4 * wordSize)); // mdx
  } else {
    __ sd(zr, Address(sp, 4 * wordSize)); // mdx
  }
  __ sd(xmethod, Address(sp, 5 * wordSize)); // method

  __ sd(zr, Address(sp, 6 * wordSize)); // last_sp
  __ sd(x19_sender_sp, Address(sp, 7 * wordSize)); // sender_sp

  __ sd(fp, Address(sp, 8 * wordSize)); // old fp
  __ sd(ra, Address(sp, 9 * wordSize)); // ra
  __ addi(fp, sp, 10 * wordSize); // fp points to sp + 10*wordSize

  // Move SP out of the way
  if (!native_call) {
    __ ld(t0, Address(xmethod, Method::const_offset()));
    __ lhu(t0, Address(t0, ConstMethod::max_stack_offset()));
    __ addi(t0, t0, frame::interpreter_frame_monitor_size() + (EnableInvokeDynamic ? 2 : 0));
    __ slli(t0, t0, Interpreter::logStackElementSize);
    __ sub(t0, sp, t0);
    __ andi(sp, t0, -16);
  }
}

address InterpreterGenerator::generate_accessor_entry(void) {
  return NULL;
}

address InterpreterGenerator::generate_Reference_get_entry(void) {
#if INCLUDE_ALL_GCS
  address entry = __ pc();

  const int referent_offset = java_lang_ref_Reference::referent_offset;
  guarantee(referent_offset > 0, "referent offset not initialized");

  if (UseG1GC) {
    Label slow_path;
    const Register local_0 = c_rarg0;
    // Check if local 0 != NULL
    __ ld(local_0, Address(esp, 0));
    __ beqz(local_0, slow_path);

    // Load the value of the referent field.
    const Address field_address(local_0, referent_offset);
    __ load_heap_oop(local_0, field_address);

    __ mv(x9, x19_sender_sp);
    __ subi(sp, sp, 2 * wordSize);
    __ sd(ra, Address(sp, wordSize));
    __ sd(fp, Address(sp, 0));
    __ mv(fp, sp);

    __ g1_write_barrier_pre(noreg /* obj */,
                            local_0 /* pre_val */,
                            xthread /* thread */,
                            t0 /* tmp */,
                            true /* tosca_live */,
                            true /* expand_call */);

    __ ld(fp, Address(sp, 0));
    __ ld(ra, Address(sp, wordSize));
    __ addi(sp, sp, 2 * wordSize);

    __ andi(sp, x9, -16);
    __ ret();

    __ bind(slow_path);
    (void) generate_normal_entry(false);

    return entry;
  }
#endif // INCLUDE_ALL_GCS

  return generate_accessor_entry();
}

address InterpreterGenerator::generate_CRC32_update_entry() {
  return NULL;
}

address InterpreterGenerator::generate_CRC32_updateBytes_entry(AbstractInterpreter::MethodKind kind) {
  return NULL;
}

void InterpreterGenerator::bang_stack_shadow_pages(bool native_call) {
  if (UseStackBanging) {
    const int start_page = native_call ? StackShadowPages : 1;
    const int page_size = os::vm_page_size();
    for (int pages = start_page; pages <= StackShadowPages; pages++) {
      __ bang_stack_with_offset(pages * page_size);
    }
  }
}

address InterpreterGenerator::generate_native_entry(bool synchronized) {
  bool inc_counter = UseCompiler || CountCompiledCalls;
  address entry_point = __ pc();

  const Address constMethod       (xmethod, Method::const_offset());
  const Address access_flags      (xmethod, Method::access_flags_offset());
  const Address size_of_parameters(x12, ConstMethod::size_of_parameters_offset());

  // get parameter size
  __ ld(x12, constMethod);
  __ lhu(x12, size_of_parameters);

  // compute beginning of parameters (xlocals)
  __ shift_left_add(xlocals, x12, esp, 3);
  __ subi(xlocals, xlocals, wordSize);

  // Pull SP back to minimum size
  __ andi(sp, esp, -16);

  // initialize fixed part of activation frame
  generate_fixed_frame(true);

  // make sure method is native & not abstract
#ifdef ASSERT
  __ lhu(x10, access_flags);
  {
    Label L;
    __ andi(t0, x10, JVM_ACC_NATIVE);
    __ bnez(t0, L);
    __ stop("tried to execute non-native method as native");
    __ bind(L);
  }
  {
    Label L;
    __ andi(t0, x10, JVM_ACC_ABSTRACT);
    __ beqz(t0, L);
    __ stop("tried to execute abstract method in interpreter");
    __ bind(L);
  }
#endif

  const Address do_not_unlock_if_synchronized(xthread,
        in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
  __ mv(t1, true);
  __ sb(t1, do_not_unlock_if_synchronized);

  Label invocation_counter_overflow;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow, NULL, NULL);
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);

  bang_stack_shadow_pages(true);

  __ sb(zr, do_not_unlock_if_synchronized);

  if (synchronized) {
    lock_method();
  }

  __ verify_frame_setup();
  __ notify_method_entry();

  const Register t = x18;
  const Register result_handler = x19;

  // allocate space for parameters
  __ ld(t, Address(xmethod, Method::const_offset()));
  __ lhu(t, Address(t, ConstMethod::size_of_parameters_offset()));
  __ slli(t, t, Interpreter::logStackElementSize);
  __ sub(t0, esp, t);
  __ andi(sp, t0, -16);
  __ mv(esp, t0);

  // get signature handler
  {
    Label L;
    __ ld(t, Address(xmethod, Method::signature_handler_offset()));
    __ bnez(t, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::prepare_native_call),
               xmethod);
    __ get_method(xmethod);
    __ ld(t, Address(xmethod, Method::signature_handler_offset()));
    __ bind(L);
  }

  __ jalr(t);
  __ get_method(xmethod);

  __ mv(result_handler, x10);
  __ sd(result_handler, Address(fp, frame::interpreter_frame_result_handler_offset * wordSize));

  // pass mirror handle if static call
  {
    Label L;
    __ lhu(t, Address(xmethod, Method::access_flags_offset()));
    __ andi(t0, t, JVM_ACC_STATIC);
    __ beqz(t0, L);
    __ ld(t, Address(xmethod, Method::const_offset()));
    __ ld(t, Address(t, ConstMethod::constants_offset()));
    __ ld(t, Address(t, ConstantPool::pool_holder_offset_in_bytes()));
    __ ld(t, Address(t, in_bytes(Klass::java_mirror_offset())));
    __ sd(t, Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize));
    __ addi(c_rarg1, fp, frame::interpreter_frame_oop_temp_offset * wordSize);
    __ bind(L);
  }

  // get native function entry point in x28
  {
    Label L;
    __ ld(x28, Address(xmethod, Method::native_function_offset()));
    __ movptr(t1, (intptr_t)SharedRuntime::native_method_throw_unsatisfied_link_error_entry());
    __ bne(x28, t1, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::prepare_native_call),
               xmethod);
    __ get_method(xmethod);
    __ ld(x28, Address(xmethod, Method::native_function_offset()));
    __ bind(L);
  }

  // pass JNIEnv
  __ addi(c_rarg0, xthread, in_bytes(JavaThread::jni_environment_offset()));

  Label native_return;
  __ set_last_Java_frame(esp, fp, native_return, t0);

  // Change state to native
  __ mv(t1, _thread_in_native);
  __ la(t0, Address(xthread, JavaThread::thread_state_offset()));
  __ sw(t1, Address(t0, 0));
  __ membar(MacroAssembler::StoreStore);

  // Call the native method
  __ jalr(x28);
  __ bind(native_return);
  __ get_method(xmethod);

  // make room for results
  __ subi(t0, esp, 4 * wordSize);
  __ andi(sp, t0, -16);

  __ push(dtos);
  __ push(ltos);

  // change thread state to native_trans
  __ mv(t1, _thread_in_native_trans);
  __ la(t0, Address(xthread, JavaThread::thread_state_offset()));
  __ sw(t1, Address(t0, 0));

  __ serialize_memory(xthread, t0);

  // check for safepoint operation in progress
  {
    Label Continue;
    __ movptr(t0, (intptr_t)SafepointSynchronize::address_of_state());
    __ lwu(t0, Address(t0, 0));
    __ beqz(t0, Continue);

    __ mv(c_rarg0, xthread);
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, JavaThread::check_safepoint_and_suspend_for_native_trans), c_rarg0);
    __ get_method(xmethod);
    __ reinit_heapbase();
    __ bind(Continue);
  }

  // change thread state to _thread_in_Java
  __ mv(t1, _thread_in_Java);
  __ la(t0, Address(xthread, JavaThread::thread_state_offset()));
  __ sw(t1, Address(t0, 0));
  __ membar(MacroAssembler::StoreStore);

  __ reset_last_Java_frame(true);

  // reset handle block
  __ ld(t, Address(xthread, JavaThread::active_handles_offset()));
  __ sd(zr, Address(t, JNIHandleBlock::top_offset_in_bytes()));

  // If result is an oop unbox and store it in frame where gc will see it
  // and result handler will pick it up
  {
    Label no_oop, not_weak, store_result;
    __ ld(result_handler, Address(fp, frame::interpreter_frame_result_handler_offset * wordSize));
    __ movptr(t, (intptr_t)AbstractInterpreter::result_handler(T_OBJECT));
    __ bne(t, result_handler, no_oop);

    // Unbox oop result, e.g. JNIHandles::resolve result.
    __ pop(ltos);
    __ beqz(x10, store_result);   // Use NULL as-is.
    STATIC_ASSERT(JNIHandles::weak_tag_mask == 1u);
    __ test_bit(t0, x10, 0);      // Test for jweak tag.
    __ beqz(t0, not_weak);

    // Resolve jweak.
    __ ld(x10, Address(x10, -JNIHandles::weak_tag_value));
#if INCLUDE_ALL_GCS
    if (UseG1GC) {
      __ subi(sp, sp, 2 * wordSize);
      __ sd(ra, Address(sp, wordSize));
      __ sd(fp, Address(sp, 0));
      __ mv(fp, sp);

      __ g1_write_barrier_pre(noreg /* obj */,
                              x10 /* pre_val */,
                              xthread /* thread */,
                              t /* tmp */,
                              true /* tosca_live */,
                              true /* expand_call */);

      __ ld(fp, Address(sp, 0));
      __ ld(ra, Address(sp, wordSize));
      __ addi(sp, sp, 2 * wordSize);
    }
#endif // INCLUDE_ALL_GCS
    __ j(store_result);

    __ bind(not_weak);
    // Resolve (untagged) jobject.
    __ ld(x10, Address(x10, 0));

    __ bind(store_result);
    __ sd(x10, Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize));
    // keep stack depth as expected by pushing oop which will eventually be discarded
    __ push(ltos);

    __ bind(no_oop);
  }

  {
    Label no_reguard;
    __ lbu(t0, Address(xthread, in_bytes(JavaThread::stack_guard_state_offset())));
    __ mv(t1, JavaThread::stack_guard_yellow_disabled);
    __ bne(t0, t1, no_reguard);

    __ subi(sp, sp, 2 * wordSize);
    __ sd(ra, Address(sp, wordSize));
    __ sd(fp, Address(sp, 0));
    __ mv(fp, sp);

    __ mv(c_rarg0, xthread);
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages), c_rarg0);

    __ ld(fp, Address(sp, 0));
    __ ld(ra, Address(sp, wordSize));
    __ addi(sp, sp, 2 * wordSize);

    __ bind(no_reguard);
  }

  // handle exceptions
  {
    Label L;
    __ ld(t0, Address(xthread, Thread::pending_exception_offset()));
    __ beqz(t0, L);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }

  // unlock if synchronized
  if (synchronized) {
    const Address monitor(fp, (intptr_t)(frame::interpreter_frame_initial_sp_offset *
                                         wordSize - sizeof(BasicObjectLock)));
    __ la(c_rarg1, monitor);
    __ unlock_object(c_rarg1);
  }

  // notify jvmt/dtrace exit
  __ notify_method_exit(vtos, InterpreterMacroAssembler::NotifyJVMTI);

  // restore result
  __ pop(ltos);
  __ pop(dtos);

  // call result handler
  __ ld(t0, Address(fp, frame::interpreter_frame_result_handler_offset * wordSize));
  __ jalr(t0);

  __ remove_activation(vtos);
  __ ret();

  if (inc_counter) {
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(&continue_after_compile);
  }

  return entry_point;
}

address InterpreterGenerator::generate_normal_entry(bool synchronized) {
  const bool inc_counter  = UseCompiler || CountCompiledCalls;
  address entry_point = __ pc();

  const Address constMethod(xmethod, Method::const_offset());
  const Address access_flags(xmethod, Method::access_flags_offset());
  const Address size_of_parameters(x13, ConstMethod::size_of_parameters_offset());
  const Address size_of_locals(x13, ConstMethod::size_of_locals_offset());

  // get parameter size
  __ ld(x13, constMethod);
  __ lhu(x12, size_of_parameters);

  __ lhu(x13, size_of_locals);
  __ sub(x13, x13, x12); // x13 = no. of additional locals

  generate_stack_overflow_check();

  // compute beginning of parameters (xlocals)
  __ shift_left_add(xlocals, x12, esp, 3);
  __ subi(xlocals, xlocals, wordSize);

  // Make room for additional locals
  __ slli(t1, x13, 3);
  __ sub(t0, esp, t1);
  __ andi(sp, t0, -16);

  // explicitly initialize locals
  {
    Label exit, loop;
    __ blez(x13, exit);
    __ bind(loop);
    __ sd(zr, Address(t0));
    __ addi(t0, t0, wordSize);
    __ subi(x13, x13, 1);
    __ bnez(x13, loop);
    __ bind(exit);
  }

  __ get_dispatch();
  generate_fixed_frame(false);

#ifdef ASSERT
  __ lhu(x10, access_flags);
  {
    Label L;
    __ andi(t0, x10, JVM_ACC_NATIVE);
    __ beqz(t0, L);
    __ stop("tried to execute native method as non-native");
    __ bind(L);
  }
  {
    Label L;
    __ andi(t0, x10, JVM_ACC_ABSTRACT);
    __ beqz(t0, L);
    __ stop("tried to execute abstract method in interpreter");
    __ bind(L);
  }
#endif

  const Address do_not_unlock_if_synchronized(xthread,
        in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
  __ mv(t1, true);
  __ sb(t1, do_not_unlock_if_synchronized);

  Label invocation_counter_overflow;
  Label profile_method;
  Label profile_method_continue;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow,
                          &profile_method,
                          &profile_method_continue);
    if (ProfileInterpreter) {
      __ bind(profile_method_continue);
    }
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);

  bang_stack_shadow_pages(false);

  __ sb(zr, do_not_unlock_if_synchronized);

  if (synchronized) {
    lock_method();
  }

  __ verify_frame_setup();
  __ notify_method_entry();

  __ dispatch_next(vtos);

  if (inc_counter) {
    if (ProfileInterpreter) {
      __ bind(profile_method);
      __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::profile_method));
      __ set_method_data_pointer_for_bcp();
      __ get_method(xmethod);
      __ j(profile_method_continue);
    }
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(&continue_after_compile);
  }

  return entry_point;
}

address AbstractInterpreterGenerator::generate_method_entry(
                                        AbstractInterpreter::MethodKind kind) {
  bool synchronized = false;
  address entry_point = NULL;

  switch (kind) {
  case Interpreter::zerolocals             :                                                                             break;
  case Interpreter::zerolocals_synchronized: synchronized = true;                                                        break;
  case Interpreter::native                 : entry_point = ((InterpreterGenerator*) this)->generate_native_entry(false); break;
  case Interpreter::native_synchronized    : entry_point = ((InterpreterGenerator*) this)->generate_native_entry(true);  break;
  case Interpreter::empty                  : entry_point = ((InterpreterGenerator*) this)->generate_empty_entry();       break;
  case Interpreter::accessor               : entry_point = ((InterpreterGenerator*) this)->generate_accessor_entry();    break;
  case Interpreter::abstract               : entry_point = ((InterpreterGenerator*) this)->generate_abstract_entry();    break;

  case Interpreter::java_lang_math_sin     : // fall thru
  case Interpreter::java_lang_math_cos     : // fall thru
  case Interpreter::java_lang_math_tan     : // fall thru
  case Interpreter::java_lang_math_abs     : // fall thru
  case Interpreter::java_lang_math_log     : // fall thru
  case Interpreter::java_lang_math_log10   : // fall thru
  case Interpreter::java_lang_math_sqrt    : // fall thru
  case Interpreter::java_lang_math_pow     : // fall thru
  case Interpreter::java_lang_math_exp     : entry_point = ((InterpreterGenerator*) this)->generate_math_entry(kind);    break;
  case Interpreter::java_lang_ref_reference_get
                                           : entry_point = ((InterpreterGenerator*)this)->generate_Reference_get_entry(); break;
  case Interpreter::java_util_zip_CRC32_update
                                           : entry_point = ((InterpreterGenerator*)this)->generate_CRC32_update_entry();  break;
  case Interpreter::java_util_zip_CRC32_updateBytes
                                           : // fall thru
  case Interpreter::java_util_zip_CRC32_updateByteBuffer
                                           : entry_point = ((InterpreterGenerator*)this)->generate_CRC32_updateBytes_entry(kind); break;
  default                                  : ShouldNotReachHere();                                                       break;
  }

  if (entry_point) {
    return entry_point;
  }

  return ((InterpreterGenerator*) this)->generate_normal_entry(synchronized);
}

bool AbstractInterpreter::can_be_compiled(methodHandle m) {
  switch (method_kind(m)) {
    case Interpreter::java_lang_math_sin     : // fall thru
    case Interpreter::java_lang_math_cos     : // fall thru
    case Interpreter::java_lang_math_tan     : // fall thru
    case Interpreter::java_lang_math_abs     : // fall thru
    case Interpreter::java_lang_math_log     : // fall thru
    case Interpreter::java_lang_math_log10   : // fall thru
    case Interpreter::java_lang_math_sqrt    : // fall thru
    case Interpreter::java_lang_math_pow     : // fall thru
    case Interpreter::java_lang_math_exp     :
      return false;
    default:
      return true;
  }
}

int AbstractInterpreter::size_top_interpreter_activation(Method* method) {
  const int entry_size = frame::interpreter_frame_monitor_size();
  const int overhead_size =
    -(frame::interpreter_frame_initial_sp_offset) + entry_size;

  const int stub_code = frame::entry_frame_after_call_words;
  const int method_stack = (method->max_locals() + method->max_stack()) *
                           Interpreter::stackElementWords;
  return (overhead_size + method_stack + stub_code);
}

int AbstractInterpreter::size_activation(int max_stack,
                                         int temps,
                                         int extra_args,
                                         int monitors,
                                         int callee_params,
                                         int callee_locals,
                                         bool is_top_frame) {
  int overhead = frame::sender_sp_offset -
                 frame::interpreter_frame_initial_sp_offset;
  int size = overhead +
         (callee_locals - callee_params) +
         monitors * frame::interpreter_frame_monitor_size() +
         (is_top_frame ? max_stack : temps + extra_args);

  size = round_to(size, 2);
  return size;
}

void AbstractInterpreter::layout_activation(Method* method,
                                            int tempcount,
                                            int popframe_extra_args,
                                            int moncount,
                                            int caller_actual_parameters,
                                            int callee_param_count,
                                            int callee_locals,
                                            frame* caller,
                                            frame* interpreter_frame,
                                            bool is_top_frame,
                                            bool is_bottom_frame) {
  int max_locals = method->max_locals() * Interpreter::stackElementWords;
  int extra_locals = (method->max_locals() - method->size_of_parameters()) *
    Interpreter::stackElementWords;

#ifdef ASSERT
  assert(caller->sp() == interpreter_frame->sender_sp(), "Frame not properly walkable");
#endif

  interpreter_frame->interpreter_frame_set_method(method);
  intptr_t* locals = interpreter_frame->sender_sp() + max_locals - 1;

#ifdef ASSERT
  if (caller->is_interpreted_frame()) {
    assert(locals < caller->fp() + frame::interpreter_frame_initial_sp_offset, "bad placement");
  }
#endif

  interpreter_frame->interpreter_frame_set_locals(locals);
  BasicObjectLock* montop = interpreter_frame->interpreter_frame_monitor_begin();
  BasicObjectLock* monbot = montop - moncount;
  interpreter_frame->interpreter_frame_set_monitor_end(monbot);

  intptr_t* esp = (intptr_t*) monbot -
    tempcount*Interpreter::stackElementWords -
    popframe_extra_args;
  interpreter_frame->interpreter_frame_set_last_sp(esp);

  if (extra_locals != 0 &&
      interpreter_frame->sender_sp() ==
      interpreter_frame->interpreter_frame_sender_sp()) {
    interpreter_frame->set_interpreter_frame_sender_sp(caller->sp() + extra_locals);
  }
  *interpreter_frame->interpreter_frame_cache_addr() =
    method->constants()->cache();
}

void TemplateInterpreterGenerator::generate_throw_exception() {
  // Entry point in previous activation
  Interpreter::_rethrow_exception_entry = __ pc();
  __ sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ reinit_heapbase();
  __ get_dispatch();

  // Entry point for exceptions thrown within interpreter code
  Interpreter::_throw_exception_entry = __ pc();
  __ get_method(xmethod);
  __ verify_oop(x10);
  __ mv(c_rarg1, x10);

  __ empty_expression_stack();
  __ call_VM(x13,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::exception_handler_for_exception),
             c_rarg1);

  // Restore machine SP
  __ ld(t0, Address(xmethod, Method::const_offset()));
  __ lhu(t0, Address(t0, ConstMethod::max_stack_offset()));
  __ addi(t0, t0, frame::interpreter_frame_monitor_size() + 2);
  __ slli(t0, t0, Interpreter::logStackElementSize);
  __ ld(t1, Address(fp, frame::interpreter_frame_initial_sp_offset * wordSize));
  __ sub(t0, t1, t0);
  __ andi(sp, t0, -16);

  __ push_ptr(x13);
  __ jr(x10);

  // JVMTI PopFrame support
  Interpreter::_remove_activation_preserving_args_entry = __ pc();
  __ empty_expression_stack();
  __ restore_bcp();
  __ lwu(x13, Address(xthread, JavaThread::popframe_condition_offset()));
  __ ori(x13, x13, JavaThread::popframe_processing_bit);
  __ sw(x13, Address(xthread, JavaThread::popframe_condition_offset()));

  {
    Label caller_not_deoptimized;
    __ ld(c_rarg1, Address(fp, frame::return_addr_offset * wordSize));
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::interpreter_contains), c_rarg1);
    __ bnez(x10, caller_not_deoptimized);

    __ get_method(x10);
    __ ld(x10, Address(x10, Method::const_offset()));
    __ lhu(x10, Address(x10, in_bytes(ConstMethod::size_of_parameters_offset())));
    __ slli(x10, x10, Interpreter::logStackElementSize);
    __ restore_locals();
    __ sub(xlocals, xlocals, x10);
    __ addi(xlocals, xlocals, wordSize);
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, Deoptimization::popframe_preserve_args),
                    xthread, x10, xlocals);

    __ remove_activation(vtos, false, false, false);
    __ mv(t0, (int64_t)JavaThread::popframe_force_deopt_reexecution_bit);
    __ sw(t0, Address(xthread, JavaThread::popframe_condition_offset()));
    __ ret();

    __ bind(caller_not_deoptimized);
  }

  __ remove_activation(vtos, false, false, false);
  __ ld(esp, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));

  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(xmethod);
  __ get_dispatch();

  if (ProfileInterpreter) {
    __ set_method_data_pointer_for_bcp();
  }

  __ sw(zr, Address(xthread, JavaThread::popframe_condition_offset()));
  assert(JavaThread::popframe_inactive == 0, "fix popframe_inactive");

#if INCLUDE_JVMTI
  {
    Label L_done;
    __ lbu(t0, Address(xbcp, 0));
    __ mv(t1, (int)Bytecodes::_invokestatic);
    __ bne(t1, t0, L_done);

    __ ld(c_rarg0, Address(xlocals, 0));
    __ call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::member_name_arg_or_null), c_rarg0, xmethod, xbcp);
    __ beqz(x10, L_done);
    __ sd(x10, Address(esp, 0));
    __ bind(L_done);
  }
#endif

  // Restore machine SP
  __ ld(t0, Address(xmethod, Method::const_offset()));
  __ lhu(t0, Address(t0, ConstMethod::max_stack_offset()));
  __ addi(t0, t0, frame::interpreter_frame_monitor_size() + (EnableInvokeDynamic ? 2 : 0));
  __ slli(t0, t0, Interpreter::logStackElementSize);
  __ ld(t1, Address(fp, frame::interpreter_frame_initial_sp_offset * wordSize));
  __ sub(t0, t1, t0);
  __ andi(sp, t0, -16);

  __ dispatch_next(vtos);
  // end of PopFrame support

  Interpreter::_remove_activation_entry = __ pc();

  // preserve exception over this code sequence
  __ pop_ptr(x10);
  __ sd(x10, Address(xthread, JavaThread::vm_result_offset()));
  // remove the activation (without doing throws on illegalMonitorExceptions)
  __ remove_activation(vtos, false, true, false);
  // restore exception
  __ get_vm_result(x10, xthread);

  // In between activations - previous activation type unknown yet
  // compute continuation point - the continuation point expects the
  // following registers set up:
  //
  // x10: exception
  // ra: return address/pc that threw exception
  // sp: expression stack of caller
  // fp: fp of caller
  __ subi(sp, sp, 2 * wordSize);
  __ sd(x10, Address(sp, 0));                   // save exception
  __ sd(ra, Address(sp, wordSize));             // save return address
  __ MacroAssembler::call_VM_leaf(CAST_FROM_FN_PTR(address,
                                                   SharedRuntime::exception_handler_for_return_address),
                                  xthread, ra);
  __ mv(x11, x10);                              // save exception handler
  __ ld(x10, Address(sp, 0));                   // restore exception
  __ ld(ra, Address(sp, wordSize));             // restore return address
  __ addi(sp, sp, 2 * wordSize);
  // We might be returning to a deopt handler that expects x13 to
  // contain the exception pc
  __ mv(x13, ra);
  __ jr(x11);                                   // jump to exception handler of caller
}

address TemplateInterpreterGenerator::generate_earlyret_entry_for(TosState state) {
  address entry = __ pc();

  __ restore_bcp();
  __ restore_locals();
  __ empty_expression_stack();
  __ load_earlyret_value(state);

  __ ld(t0, Address(xthread, JavaThread::jvmti_thread_state_offset()));
  Address cond_addr(t0, in_bytes(JvmtiThreadState::earlyret_state_offset()));

  assert(JvmtiThreadState::earlyret_inactive == 0, "should be");
  __ sw(zr, cond_addr);

  __ remove_activation(state, false, false, true);
  __ ret();

  return entry;
}

void TemplateInterpreterGenerator::set_vtos_entry_points(Template* t,
                                                         address& bep,
                                                         address& cep,
                                                         address& sep,
                                                         address& aep,
                                                         address& iep,
                                                         address& lep,
                                                         address& fep,
                                                         address& dep,
                                                         address& vep) {
  assert(t != NULL && t->is_valid() && t->tos_in() == vtos, "illegal template");
  Label L;
  aep = __ pc();
  __ push_ptr();
  __ j(L);
  fep = __ pc();
  __ push_f();
  __ j(L);
  dep = __ pc();
  __ push_d();
  __ j(L);
  lep = __ pc();
  __ push_l();
  __ j(L);
  bep = cep = sep = iep = __ pc();
  __ push_i();
  vep = __ pc();
  __ bind(L);
  generate_and_dispatch(t);
}

InterpreterGenerator::InterpreterGenerator(StubQueue* code)
 : TemplateInterpreterGenerator(code) {
   generate_all();
}

#ifndef PRODUCT
address TemplateInterpreterGenerator::generate_trace_code(TosState state) {
  address entry = __ pc();
  __ subi(sp, sp, 2 * wordSize);
  __ sd(ra, Address(sp, wordSize));
  __ sd(fp, Address(sp, 0));
  __ mv(fp, sp);

  __ push(state);
  __ mv(c_rarg2, x10);  // Pass itos
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address, SharedRuntime::trace_bytecode),
             c_rarg1, c_rarg2, c_rarg3);
  __ pop(state);

  __ ld(fp, Address(sp, 0));
  __ ld(ra, Address(sp, wordSize));
  __ addi(sp, sp, 2 * wordSize);
  __ ret();

  return entry;
}

void TemplateInterpreterGenerator::count_bytecode() {
  __ subi(sp, sp, 2 * wordSize);
  __ sd(t0, Address(sp, 0));
  __ sd(t1, Address(sp, wordSize));
  __ movptr(t0, (intptr_t)&BytecodeCounter::_counter_value);
  __ ld(t1, Address(t0, 0));
  __ addi(t1, t1, 1);
  __ sd(t1, Address(t0, 0));
  __ ld(t0, Address(sp, 0));
  __ ld(t1, Address(sp, wordSize));
  __ addi(sp, sp, 2 * wordSize);
}

void TemplateInterpreterGenerator::histogram_bytecode(Template* t) { ; }
void TemplateInterpreterGenerator::histogram_bytecode_pair(Template* t) { ; }

void TemplateInterpreterGenerator::trace_bytecode(Template* t) {
  assert(Interpreter::trace_code(t->tos_in()) != NULL, "entry must have been generated");
  __ far_call(RuntimeAddress(Interpreter::trace_code(t->tos_in())));
  __ reinit_heapbase();
}

void TemplateInterpreterGenerator::stop_interpreter_at() {
  Label L;
  __ movptr(t0, (intptr_t)&BytecodeCounter::_counter_value);
  __ ld(t0, Address(t0, 0));
  __ movptr(t1, StopInterpreterAt);
  __ bne(t0, t1, L);
  __ ebreak();
  __ bind(L);
}
#endif // !PRODUCT

#endif // !CC_INTERP
