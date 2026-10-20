/*
 * Copyright (c) 2000, 2022, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2020, Red Hat Inc. All rights reserved.
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
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_Compilation.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "c1/c1_ValueStack.hpp"
#include "ci/ciArrayKlass.hpp"
#include "ci/ciInstance.hpp"
#include "code/compiledIC.hpp"
#include "gc_interface/collectedHeap.hpp"
#include "memory/barrierSet.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/objArrayKlass.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_riscv.inline.hpp"

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

NEEDS_CLEANUP // remove this definitions ?
const Register IC_Klass    = xmethod; // where the IC klass is cached
const Register SYNC_header = x10;  // synchronization header
const Register SHIFT_count = x10;  // where count for shift operations must be

#define __ _masm->

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2) {
  if (tmp1 == preserve) {
    assert_different_registers(tmp1, tmp2, extra);
    tmp1 = extra;
  } else if (tmp2 == preserve) {
    assert_different_registers(tmp1, tmp2, extra);
    tmp2 = extra;
  }
  assert_different_registers(preserve, tmp1, tmp2);
}

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2,
                                       Register &tmp3) {
  if (tmp1 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp1 = extra;
  } else if (tmp2 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp2 = extra;
  } else if (tmp3 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp3 = extra;
  }
  assert_different_registers(preserve, tmp1, tmp2, tmp3);
}

bool LIR_Assembler::is_small_constant(LIR_Opr opr) {
  Unimplemented();
  return false;
}

LIR_Opr LIR_Assembler::receiverOpr() {
  return FrameMap::receiver_opr;
}

LIR_Opr LIR_Assembler::osrBufferPointer() {
  return FrameMap::as_pointer_opr(receiverOpr()->as_register());
}

//--------------fpu register translations-----------------------

address LIR_Assembler::float_constant(float f) {
  address const_addr = __ float_constant(f);
  if (const_addr == NULL) {
    bailout("const section overflow");
    return __ code()->consts()->start();
  } else {
    return const_addr;
  }
}

address LIR_Assembler::double_constant(double d) {
  address const_addr = __ double_constant(d);
  if (const_addr == NULL) {
    bailout("const section overflow");
    return __ code()->consts()->start();
  } else {
    return const_addr;
  }
}

address LIR_Assembler::int_constant(jlong n) {
  address const_addr = __ long_constant(n);
  if (const_addr == NULL) {
    bailout("const section overflow");
    return __ code()->consts()->start();
  } else {
    return const_addr;
  }
}

void LIR_Assembler::set_24bit_FPU() { Unimplemented(); }
void LIR_Assembler::reset_FPU() { Unimplemented(); }
void LIR_Assembler::fpop() { Unimplemented(); }
void LIR_Assembler::fxch(int i) { Unimplemented(); }
void LIR_Assembler::fld(int i) { Unimplemented(); }
void LIR_Assembler::ffree(int i) { Unimplemented(); }

void LIR_Assembler::breakpoint() { Unimplemented(); }

void LIR_Assembler::push(LIR_Opr opr) { Unimplemented(); }

void LIR_Assembler::pop(LIR_Opr opr) { Unimplemented(); }

bool LIR_Assembler::is_literal_address(LIR_Address* addr) { Unimplemented(); return false; }

//-------------------------------------------
static jlong as_long(LIR_Opr data) {
  jlong result;
  switch (data->type()) {
    case T_INT:
      result = (data->as_jint());
      break;
    case T_LONG:
      result = (data->as_jlong());
      break;
    default:
      ShouldNotReachHere();
      result = 0; // unreachable
  }
  return result;
}

Address LIR_Assembler::as_Address(LIR_Address* addr, Register tmp) {
  if (addr->base()->is_illegal()) {
    assert(addr->index()->is_illegal(), "must be illegal too");
    __ movptr(tmp, addr->disp());
    return Address(tmp, 0);
  }

  Register base = addr->base()->as_pointer_register();
  LIR_Opr index_opr = addr->index();

  if (index_opr->is_illegal()) {
    intptr_t addr_offset = intptr_t(addr->disp());
    if (Assembler::is_simm12(addr_offset)) {
      return Address(base, addr_offset);
    } else {
      assert_different_registers(base, tmp);
      __ mv(tmp, addr_offset);
      __ add(tmp, base, tmp);
      return Address(tmp, 0);
    }
  }

  int scale = addr->scale();
  if (index_opr->is_cpu_register()) {
    Register index;
    if (index_opr->is_single_cpu()) {
      index = index_opr->as_register();
    } else {
      index = index_opr->as_register_lo();
    }
    assert_different_registers(base, tmp);
    if (scale != 0) {
      __ shadd(tmp, index, base, tmp, scale);
    } else {
      __ add(tmp, base, index);
    }
    intptr_t addr_offset = intptr_t(addr->disp());
    if (Assembler::is_simm12(addr_offset)) {
      return Address(tmp, addr_offset);
    } else {
      __ add(tmp, tmp, addr_offset);
      return Address(tmp, 0);
    }
  } else if (index_opr->is_constant()) {
    intptr_t addr_offset = (((intptr_t)index_opr->as_constant_ptr()->as_jint()) << scale) + addr->disp();
    if (Assembler::is_simm12(addr_offset)) {
      return Address(base, addr_offset);
    } else {
      assert_different_registers(base, tmp);
      __ mv(tmp, addr_offset);
      __ add(tmp, base, tmp);
      return Address(tmp, 0);
    }
  }

  Unimplemented();
  return Address();
}

Address LIR_Assembler::as_Address_hi(LIR_Address* addr) {
  ShouldNotReachHere();
  return Address();
}

Address LIR_Assembler::as_Address(LIR_Address* addr) {
  return as_Address(addr, t0);
}

Address LIR_Assembler::as_Address_lo(LIR_Address* addr) {
  return as_Address(addr, t0);
}

// Ensure a valid Address (base + offset) to a stack-slot. If stack access is
// not encodable as a base + (immediate) offset, generate an explicit address
// calculation to hold the address in a temporary register.
Address LIR_Assembler::stack_slot_address(int index, uint size, int adjust) {
  precond(size == 4 || size == 8);
  Address addr = frame_map()->address_for_slot(index, adjust);
  precond(addr.getMode() == Address::base_plus_offset);
  precond(addr.base() == sp);
  precond(addr.offset() > 0);
  uint mask = size - 1;
  assert((addr.offset() & mask) == 0, "scaled offsets only");

  return addr;
}

void LIR_Assembler::osr_entry() {
  offsets()->set_value(CodeOffsets::OSR_Entry, code_offset());
  BlockBegin* osr_entry = compilation()->hir()->osr_entry();
  ValueStack* entry_state = osr_entry->state();
  int number_of_locks = entry_state->locks_size();

  // we jump here if osr happens with the interpreter
  // state set up to continue at the beginning of the
  // loop that triggered osr - in particular, we have
  // the following registers setup:
  //
  // x12: osr buffer
  //

  // build frame
  ciMethod* m = compilation()->method();
  __ build_frame(initial_frame_size_in_bytes(), bang_size_in_bytes());

  // OSR buffer is
  //
  // locals[nlocals-1..0]
  // monitors[0..number_of_locks]
  //
  // locals is a direct copy of the interpreter frame so in the osr buffer
  // so first slot in the local array is the last local from the interpreter
  // and last slot is local[0] (receiver) from the interpreter
  //
  // Similarly with locks. The first lock slot in the osr buffer is the nth lock
  // from the interpreter frame, the nth lock slot in the osr buffer is 0th lock
  // in the interpreter frame (the method lock if a sync method)

  // Initialize monitors in the compiled activation.
  //   x12: pointer to osr buffer
  //
  // All other registers are dead at this point and the locals will be
  // copied into place by code emitted in the IR.

  Register OSR_buf = osrBufferPointer()->as_pointer_register();
  { assert(frame::interpreter_frame_monitor_size() == BasicObjectLock::size(), "adjust code below");
    int monitor_offset = BytesPerWord * method()->max_locals() +
      (2 * BytesPerWord) * (number_of_locks - 1);
    // SharedRuntime::OSR_migration_begin() packs BasicObjectLocks in
    // the OSR buffer using 2 word entries: first the lock and then
    // the oop.
    for (int i = 0; i < number_of_locks; i++) {
      int slot_offset = monitor_offset - ((i * 2) * BytesPerWord);
#ifdef ASSERT
      // verify the interpreter's monitor has a non-null object
      {
        Label L;
        __ ld(t0, Address(OSR_buf, slot_offset + 1 * BytesPerWord));
        __ bnez(t0, L);
        __ stop("locked object is NULL");
        __ bind(L);
      }
#endif
      __ ld(x9, Address(OSR_buf, slot_offset + 0));
      __ sd(x9, frame_map()->address_for_monitor_lock(i));
      __ ld(x9, Address(OSR_buf, slot_offset + 1*BytesPerWord));
      __ sd(x9, frame_map()->address_for_monitor_object(i));
    }
  }
}

// inline cache check; done before the frame is built.
int LIR_Assembler::check_icache() {
  Register receiver = FrameMap::receiver_opr->as_register();
  Register ic_klass = IC_Klass;
  int start_offset = __ offset();
  Label dont;
  __ cmp_klass(receiver, ic_klass, t0, dont);

  // if icache check fails, then jump to runtime routine
  // Note: RECEIVER must still contain the receiver!
  __ far_jump(RuntimeAddress(SharedRuntime::get_ic_miss_stub()));

  // We align the verified entry point unless the method body
  // (including its inline cache check) will fit in a single 64-byte
  // icache line.
  if (!method()->is_accessor() || __ offset() - start_offset > 4 * 4) {
    // force alignment after the cache check.
    __ align(CodeEntryAlignment);
  }

  __ bind(dont);
  return start_offset;
}

void LIR_Assembler::jobject2reg(jobject o, Register reg) {
  if (o == NULL) {
    __ mv(reg, zr);
  } else {
    __ movoop(reg, o, /*immediate*/true);
  }
}

void LIR_Assembler::deoptimize_trap(CodeEmitInfo *info) {
  address target = NULL;

  switch (patching_id(info)) {
    case PatchingStub::access_field_id:
      target = Runtime1::entry_for(Runtime1::access_field_patching_id);
      break;
    case PatchingStub::load_klass_id:
      target = Runtime1::entry_for(Runtime1::load_klass_patching_id);
      break;
    case PatchingStub::load_mirror_id:
      target = Runtime1::entry_for(Runtime1::load_mirror_patching_id);
      break;
    case PatchingStub::load_appendix_id:
      target = Runtime1::entry_for(Runtime1::load_appendix_patching_id);
      break;
    default: ShouldNotReachHere();
  }

  __ far_call(RuntimeAddress(target));
  add_call_info_here(info);
}

void LIR_Assembler::jobject2reg_with_patching(Register reg, CodeEmitInfo *info) {
  deoptimize_trap(info);
}

// This specifies the rsp decrement needed to build the frame
int LIR_Assembler::initial_frame_size_in_bytes() const {
  // if rounding, must let FrameMap know!
  // The frame_map records size in slots (32bit word)
  // subtract two words to account for return address and link
  return (frame_map()->framesize() - (2*VMRegImpl::slots_per_word)) * VMRegImpl::stack_slot_size;
}

int LIR_Assembler::emit_exception_handler() {
  // if the last instruction is a call (typically to do a throw which
  // is coming at the end after block reordering) the return address
  // must still point into the code area in order to avoid assertion
  // failures when searching for the corresponding bci => add a nop
  // (was bug 5/14/1999 - gri)
  __ nop();

  // generate code for exception handler
  address handler_base = __ start_a_stub(_exception_handler_size);
  if (handler_base == NULL) {
    // not enough space left for the handler
    bailout("exception handler overflow");
    return -1;
  }

  int offset = code_offset();

  // the exception oop and pc are in x10 and x13
  // no other registers need to be preserved, so invalidate them
  __ invalidate_registers(false, true, true, false, true, true);

  // check that there is really an exception
  __ verify_not_null_oop(x10);

  // search an exception handler (x10: exception oop, x13: throwing pc)
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::handle_exception_from_callee_id)));
  __ should_not_reach_here();
  guarantee(code_offset() - offset <= _exception_handler_size, "overflow");
  __ end_a_stub();

  return offset;
}

// Emit the code to remove the frame from the stack in the exception
// unwind path.
int LIR_Assembler::emit_unwind_handler() {
#ifndef PRODUCT
  if (CommentedAssembly) {
    _masm->block_comment("Unwind handler");
  }
#endif

  int offset = code_offset();

  // Fetch the exception from TLS and clear out exception related thread state
  __ ld(x10, Address(xthread, JavaThread::exception_oop_offset()));
  __ sd(zr, Address(xthread, JavaThread::exception_oop_offset()));
  __ sd(zr, Address(xthread, JavaThread::exception_pc_offset()));

  __ bind(_unwind_handler_entry);
  __ verify_not_null_oop(x10);
  if (method()->is_synchronized() || compilation()->env()->dtrace_method_probes()) {
    __ mv(x9, x10);  // Preserve the exception
  }

  // Perform needed unlocking
  MonitorExitStub* stub = NULL;
  if (method()->is_synchronized()) {
    monitor_address(0, FrameMap::r10_opr);
    stub = new MonitorExitStub(FrameMap::r10_opr, true, 0);
    __ unlock_object(x15, x14, x10, *stub->entry());
    __ bind(*stub->continuation());
  }

  if (compilation()->env()->dtrace_method_probes()) {
    __ mv(c_rarg0, xthread);
    __ mov_metadata(c_rarg1, method()->constant_encoding());
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_exit), c_rarg0, c_rarg1);
  }

  if (method()->is_synchronized() || compilation()->env()->dtrace_method_probes()) {
    __ mv(x10, x9);  // Restore the exception
  }

  // remove the activation and dispatch to the unwind handler
  __ block_comment("remove_frame and dispatch to the unwind handler");
  __ remove_frame(initial_frame_size_in_bytes());
  __ far_jump(RuntimeAddress(Runtime1::entry_for(Runtime1::unwind_exception_id)));

  // Emit the slow path assembly
  if (stub != NULL) {
    stub->emit_code(this);
  }

  return offset;
}

int LIR_Assembler::emit_deopt_handler() {
  // if the last instruction is a call (typically to do a throw which
  // is coming at the end after block reordering) the return address
  // must still point into the code area in order to avoid assertion
  // failures when searching for the corresponding bci => add a nop
  // (was bug 5/14/1999 - gri)
  __ nop();

  // generate code for exception handler
  address handler_base = __ start_a_stub(_deopt_handler_size);
  if (handler_base == NULL) {
    // not enough space left for the handler
    bailout("deopt handler overflow");
    return -1;
  }

  int offset = code_offset();

  __ auipc(ra, 0);
  __ far_jump(RuntimeAddress(SharedRuntime::deopt_blob()->unpack()));
  guarantee(code_offset() - offset <= _deopt_handler_size, "overflow");
  __ end_a_stub();

  return offset;
}

void LIR_Assembler::emit_string_compare(LIR_Opr arg0, LIR_Opr arg1, LIR_Opr dst, CodeEmitInfo* info) {
  __ call_Unimplemented();
}

void LIR_Assembler::add_debug_info_for_branch(address adr, CodeEmitInfo* info) {
  _masm->code_section()->relocate(adr, relocInfo::poll_type);
  int pc_offset = code_offset();
  flush_debug_info(pc_offset);
  info->record_debug_info(compilation()->debug_info_recorder(), pc_offset);
  if (info->exception_handlers() != NULL) {
    compilation()->add_exception_handlers_for_pco(pc_offset, info->exception_handlers());
  }
}

void LIR_Assembler::return_op(LIR_Opr result) {
  assert(result->is_illegal() || !result->is_single_cpu() || result->as_register() == x10,
         "word returns are in x10,");

  // Pop the stack before the safepoint code
  __ remove_frame(initial_frame_size_in_bytes());
  __ get_polling_page(t0, relocInfo::poll_return_type);
  __ read_polling_page(t0, 0, relocInfo::poll_return_type);
  __ ret();
}

int LIR_Assembler::safepoint_poll(LIR_Opr tmp, CodeEmitInfo* info) {
  guarantee(info != NULL, "Shouldn't be NULL");
  __ get_polling_page(t0, relocInfo::poll_type);
  add_debug_info_for_branch(info);  // This isn't just debug info: it's the oop map
  __ read_polling_page(t0, 0, relocInfo::poll_type);
  return __ offset();
}

void LIR_Assembler::move_regs(Register from_reg, Register to_reg) {
  if (from_reg != to_reg) {
    __ mv(to_reg, from_reg);
  }
}

void LIR_Assembler::swap_reg(Register a, Register b) { Unimplemented(); }

void LIR_Assembler::const2reg_helper(LIR_Opr src) {
  assert(src->is_constant(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();
  switch (c->type()) {
    case T_INT:
      __ mvw(t0, c->as_jint());
      break;
    case T_ADDRESS:
      __ mv(t0, c->as_jint());
      break;
    case T_LONG:
      __ mv(t0, (intptr_t)c->as_jlong());
      break;
    case T_OBJECT:
    case T_ARRAY:
      jobject2reg(c->as_jobject(), t0);
      break;
    case T_METADATA:
      __ mov_metadata(t0, c->as_metadata());
      break;
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::const2reg(LIR_Opr src, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();

  switch (c->type()) {
    case T_INT: {
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mvw(dest->as_register(), c->as_jint());
      break;
    }

    case T_ADDRESS: {
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mv(dest->as_register(), c->as_jint());
      break;
    }

    case T_LONG: {
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mv(dest->as_register_lo(), (intptr_t)c->as_jlong());
      break;
    }

    case T_OBJECT: {
      if (patch_code == lir_patch_none) {
        jobject2reg(c->as_jobject(), dest->as_register());
      } else {
        jobject2reg_with_patching(dest->as_register(), info);
      }
      break;
    }

    case T_METADATA: {
      if (patch_code == lir_patch_none) {
        __ mov_metadata(dest->as_register(), c->as_metadata());
      } else {
        klass2reg_with_patching(dest->as_register(), info);
      }
      break;
    }

    case T_FLOAT: {
      if (c->is_zero_float()) {
        __ fmv_w_x(dest->as_float_reg(), zr);
      } else {
        __ la(t0, InternalAddress(float_constant(c->as_jfloat())));
        __ flw(dest->as_float_reg(), Address(t0));
      }
      break;
    }

    case T_DOUBLE: {
      if (c->is_zero_double()) {
        __ fmv_d_x(dest->as_double_reg(), zr);
      } else {
        __ la(t0, InternalAddress(double_constant(c->as_jdouble())));
        __ fld(dest->as_double_reg(), Address(t0));
      }
      break;
    }

    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::const2stack(LIR_Opr src, LIR_Opr dest) {
  LIR_Const* c = src->as_constant_ptr();
  switch (c->type()) {
    case T_OBJECT: {
      if (!c->as_jobject()) {
        __ sd(zr, stack_slot_address(dest->single_stack_ix(), sizeof(uint64_t)));
      } else {
        const2reg(src, FrameMap::t1_opr, lir_patch_none, NULL);
        reg2stack(FrameMap::t1_opr, dest, c->type(), false);
      }
      break;
    }
    case T_ADDRESS: {
      const2reg(src, FrameMap::t1_opr, lir_patch_none, NULL);
      reg2stack(FrameMap::t1_opr, dest, c->type(), false);
      break;
    }
    case T_INT:
    case T_FLOAT: {
      Address addr = stack_slot_address(dest->single_stack_ix(), sizeof(uint32_t));
      if (c->as_jint_bits() == 0) {
        __ sw(zr, addr);
      } else {
        __ mvw(t1, c->as_jint_bits());
        __ sw(t1, addr);
      }
      break;
    }
    case T_LONG:
    case T_DOUBLE: {
      Address addr = stack_slot_address(dest->double_stack_ix(), sizeof(uint64_t), lo_word_offset_in_bytes);
      if (c->as_jlong_bits() == 0) {
        __ sd(zr, addr);
      } else {
        __ mv(t1, (intptr_t)c->as_jlong_bits());
        __ sd(t1, addr);
      }
      break;
    }
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::const2mem(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info, bool wide) {
  assert(src->is_constant(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();
  LIR_Address* to_addr = dest->as_address_ptr();

  void (MacroAssembler::* insn)(Register Rt, Address adr, Register tmp);

  switch (type) {
    case T_ADDRESS:
      assert(c->as_jint() == 0, "should be");
      insn = &MacroAssembler::sd;
      break;
    case T_LONG:
      assert(c->as_jlong() == 0, "should be");
      insn = &MacroAssembler::sd;
      break;
    case T_INT:
      assert(c->as_jint() == 0, "should be");
      insn = &MacroAssembler::sw;
      break;
    case T_FLOAT:
      assert(c->as_jfloat() == 0.0f, "should be");
      insn = &MacroAssembler::sw;
      break;
    case T_DOUBLE:
      assert(c->as_jdouble() == 0.0, "should be");
      insn = &MacroAssembler::sd;
      break;
    case T_OBJECT:
    case T_ARRAY:
      assert(c->as_jobject() == 0, "should be");
      if (UseCompressedOops && !wide) {
        insn = &MacroAssembler::sw;
      } else {
        insn = &MacroAssembler::sd;
      }
      break;
    case T_CHAR:
    case T_SHORT:
      assert(c->as_jint() == 0, "should be");
      insn = &MacroAssembler::sh;
      break;
    case T_BOOLEAN:
    case T_BYTE:
      assert(c->as_jint() == 0, "should be");
      insn = &MacroAssembler::sb;
      break;
    default:
      ShouldNotReachHere();
      insn = &MacroAssembler::sd;  // unreachable
  }

  if (info != NULL) {
    add_debug_info_for_null_check_here(info);
  }
  (_masm->*insn)(zr, as_Address(to_addr, t0), t0);
}

void LIR_Assembler::reg2reg(LIR_Opr src, LIR_Opr dest) {
  assert(src->is_register(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");

  // move between cpu-registers
  if (dest->is_single_cpu()) {
    if (src->type() == T_LONG) {
      // Can do LONG -> OBJECT
      move_regs(src->as_register_lo(), dest->as_register());
      return;
    }
    assert(src->is_single_cpu(), "must match");
    if (src->type() == T_OBJECT) {
      __ verify_oop(src->as_register());
    }
    move_regs(src->as_register(), dest->as_register());

  } else if (dest->is_double_cpu()) {
    if (is_reference_type(src->type())) {
      // Surprising to me but we can see move of a long to t_object
      __ verify_oop(src->as_register());
      move_regs(src->as_register(), dest->as_register_lo());
      return;
    }
    assert(src->is_double_cpu(), "must match");
    Register f_lo = src->as_register_lo();
    Register f_hi = src->as_register_hi();
    Register t_lo = dest->as_register_lo();
    Register t_hi = dest->as_register_hi();
    assert(f_hi == f_lo, "must be same");
    assert(t_hi == t_lo, "must be same");
    move_regs(f_lo, t_lo);

  } else if (dest->is_single_fpu()) {
    __ fmv_s(dest->as_float_reg(), src->as_float_reg());

  } else if (dest->is_double_fpu()) {
    __ fmv_d(dest->as_double_reg(), src->as_double_reg());

  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2stack(LIR_Opr src, LIR_Opr dest, BasicType type, bool pop_fpu_stack) {
  precond(src->is_register() && dest->is_stack());

  uint const c_sz32 = sizeof(uint32_t);
  uint const c_sz64 = sizeof(uint64_t);

  if (src->is_single_cpu()) {
    int index = dest->single_stack_ix();
    if (is_reference_type(type)) {
      __ sd(src->as_register(), stack_slot_address(index, c_sz64));
      __ verify_oop(src->as_register());
    } else if (type == T_METADATA || type == T_DOUBLE || type == T_ADDRESS) {
      __ sd(src->as_register(), stack_slot_address(index, c_sz64));
    } else {
      __ sw(src->as_register(), stack_slot_address(index, c_sz32));
    }

  } else if (src->is_double_cpu()) {
    int index = dest->double_stack_ix();
    Address dest_addr_LO = stack_slot_address(index, c_sz64, lo_word_offset_in_bytes);
    __ sd(src->as_register_lo(), dest_addr_LO);

  } else if (src->is_single_fpu()) {
    int index = dest->single_stack_ix();
    __ fsw(src->as_float_reg(), stack_slot_address(index, c_sz32));

  } else if (src->is_double_fpu()) {
    int index = dest->double_stack_ix();
    __ fsd(src->as_double_reg(), stack_slot_address(index, c_sz64));

  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2mem(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool pop_fpu_stack, bool wide, bool /* unaligned */) {
  LIR_Address* to_addr = dest->as_address_ptr();
  PatchingStub* patch = NULL;
  Register compressed_src = t1;

  if (patch_code != lir_patch_none) {
    deoptimize_trap(info);
    return;
  }

  if (is_reference_type(type)) {
    __ verify_oop(src->as_register());

    if (UseCompressedOops && !wide) {
      __ encode_heap_oop(compressed_src, src->as_register());
    } else {
      compressed_src = src->as_register();
    }
  }

  int null_check_here = code_offset();
  switch (type) {
    case T_FLOAT: {
      __ fsw(src->as_float_reg(), as_Address(to_addr));
      break;
    }

    case T_DOUBLE: {
      __ fsd(src->as_double_reg(), as_Address(to_addr));
      break;
    }

    case T_ARRAY:   // fall through
    case T_OBJECT:  // fall through
      if (UseCompressedOops && !wide) {
        __ sw(compressed_src, as_Address(to_addr));
      } else {
        __ sd(compressed_src, as_Address(to_addr));
      }
      break;
    case T_METADATA:
      // We get here to store a method pointer to the stack to pass to
      // a dtrace runtime call. This can't work on 64 bit with
      // compressed klass ptrs: T_METADATA can be a compressed klass
      // ptr or a 64 bit method pointer.
      ShouldNotReachHere();
      __ sd(src->as_register(), as_Address(to_addr));
      break;
    case T_ADDRESS:
      __ sd(src->as_register(), as_Address(to_addr));
      break;
    case T_INT:
      __ sw(src->as_register(), as_Address(to_addr));
      break;

    case T_LONG: {
      __ sd(src->as_register_lo(), as_Address_lo(to_addr));
      break;
    }

    case T_BYTE:    // fall through
    case T_BOOLEAN: {
      __ sb(src->as_register(), as_Address(to_addr));
      break;
    }

    case T_CHAR:    // fall through
    case T_SHORT:
      __ sh(src->as_register(), as_Address(to_addr));
      break;

    default:
      ShouldNotReachHere();
  }
  if (info != NULL) {
    add_debug_info_for_null_check(null_check_here, info);
  }
}

void LIR_Assembler::stack2reg(LIR_Opr src, LIR_Opr dest, BasicType type) {
  precond(src->is_stack() && dest->is_register());

  uint const c_sz32 = sizeof(uint32_t);
  uint const c_sz64 = sizeof(uint64_t);

  if (dest->is_single_cpu()) {
    int index = src->single_stack_ix();
    if (type == T_INT) {
      __ lw(dest->as_register(), stack_slot_address(index, c_sz32));
    } else if (is_reference_type(type)) {
      __ ld(dest->as_register(), stack_slot_address(index, c_sz64));
      __ verify_oop(dest->as_register());
    } else if (type == T_METADATA || type == T_ADDRESS) {
      __ ld(dest->as_register(), stack_slot_address(index, c_sz64));
    } else {
      __ lwu(dest->as_register(), stack_slot_address(index, c_sz32));
    }

  } else if (dest->is_double_cpu()) {
    int index = src->double_stack_ix();
    Address src_addr_LO = stack_slot_address(index, c_sz64, lo_word_offset_in_bytes);
    __ ld(dest->as_register_lo(), src_addr_LO);

  } else if (dest->is_single_fpu()) {
    int index = src->single_stack_ix();
    __ flw(dest->as_float_reg(), stack_slot_address(index, c_sz32));

  } else if (dest->is_double_fpu()) {
    int index = src->double_stack_ix();
    __ fld(dest->as_double_reg(), stack_slot_address(index, c_sz64));

  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::klass2reg_with_patching(Register reg, CodeEmitInfo* info) {
  address target = NULL;

  switch (patching_id(info)) {
    case PatchingStub::access_field_id:
      target = Runtime1::entry_for(Runtime1::access_field_patching_id);
      break;
    case PatchingStub::load_klass_id:
      target = Runtime1::entry_for(Runtime1::load_klass_patching_id);
      break;
    case PatchingStub::load_mirror_id:
      target = Runtime1::entry_for(Runtime1::load_mirror_patching_id);
      break;
    case PatchingStub::load_appendix_id:
      target = Runtime1::entry_for(Runtime1::load_appendix_patching_id);
      break;
    default: ShouldNotReachHere();
  }

  __ far_call(RuntimeAddress(target));
  add_call_info_here(info);
}

void LIR_Assembler::stack2stack(LIR_Opr src, LIR_Opr dest, BasicType type) {
  LIR_Opr temp;
  if (type == T_LONG || type == T_DOUBLE) {
    temp = FrameMap::t1_long_opr;
  } else {
    temp = FrameMap::t1_opr;
  }

  stack2reg(src, temp, src->type());
  reg2stack(temp, dest, dest->type(), false);
}

void LIR_Assembler::mem2reg(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool wide, bool /* unaligned */) {
  LIR_Address* addr = src->as_address_ptr();
  LIR_Address* from_addr = src->as_address_ptr();

  if (addr->base()->type() == T_OBJECT) {
    __ verify_oop(addr->base()->as_pointer_register());
  }

  if (patch_code != lir_patch_none) {
    deoptimize_trap(info);
    return;
  }

  if (info != NULL) {
    add_debug_info_for_null_check_here(info);
  }
  int null_check_here = code_offset();
  switch (type) {
    case T_FLOAT: {
      __ flw(dest->as_float_reg(), as_Address(from_addr));
      break;
    }

    case T_DOUBLE: {
      __ fld(dest->as_double_reg(), as_Address(from_addr));
      break;
    }

    case T_ARRAY:   // fall through
    case T_OBJECT:  // fall through
      if (UseCompressedOops && !wide) {
        __ lwu(dest->as_register(), as_Address(from_addr));
      } else {
        __ ld(dest->as_register(), as_Address(from_addr));
      }
      break;
    case T_METADATA:
      ShouldNotReachHere();
      __ ld(dest->as_register(), as_Address(from_addr));
      break;
    case T_ADDRESS:
      if (UseCompressedClassPointers && addr->disp() == oopDesc::klass_offset_in_bytes()) {
        __ lwu(dest->as_register(), as_Address(from_addr));
      } else {
        __ ld(dest->as_register(), as_Address(from_addr));
      }
      break;
    case T_INT:
      __ lw(dest->as_register(), as_Address(from_addr));
      break;

    case T_LONG: {
      __ ld(dest->as_register_lo(), as_Address_lo(from_addr));
      break;
    }

    case T_BYTE:
      __ lb(dest->as_register(), as_Address(from_addr));
      break;
    case T_BOOLEAN: {
      __ lbu(dest->as_register(), as_Address(from_addr));
      break;
    }

    case T_CHAR:
      __ lhu(dest->as_register(), as_Address(from_addr));
      break;
    case T_SHORT:
      __ lh(dest->as_register(), as_Address(from_addr));
      break;

    default:
      ShouldNotReachHere();
  }

  if (is_reference_type(type)) {
    if (UseCompressedOops && !wide) {
      __ decode_heap_oop(dest->as_register());
    }
    __ verify_oop(dest->as_register());
  } else if (type == T_ADDRESS && addr->disp() == oopDesc::klass_offset_in_bytes()) {
    if (UseCompressedClassPointers) {
      __ decode_klass_not_null(dest->as_register());
    }
  }
}

void LIR_Assembler::prefetchr(LIR_Opr src) { Unimplemented(); }
void LIR_Assembler::prefetchw(LIR_Opr src) { Unimplemented(); }

int LIR_Assembler::array_element_size(BasicType type) const {
  int elem_size = type2aelembytes(type);
  return exact_log2(elem_size);
}

void LIR_Assembler::emit_op3(LIR_Op3* op) {
  switch (op->code()) {
    case lir_idiv:
    case lir_irem:
      arithmetic_idiv(op->code(),
                      op->in_opr1(),
                      op->in_opr2(),
                      op->in_opr3(),
                      op->result_opr(),
                      op->info());
      break;
    default:
      ShouldNotReachHere();
      break;
  }
}

void LIR_Assembler::emit_branch(LIR_Condition cmp_flag, LIR_Opr cmp1, LIR_Opr cmp2, Label& label, bool is_far, bool is_unordered) {
  if (cmp1->is_single_fpu()) {
    assert(cmp2->is_single_fpu(), "expect single float register");
    switch (cmp_flag) {
      case lir_cond_equal:
        __ float_beq(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_notEqual:
        __ float_bne(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_less:
        __ float_blt(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_lessEqual:
        __ float_ble(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_greaterEqual:
        __ float_bge(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_greater:
        __ float_bgt(cmp1->as_float_reg(), cmp2->as_float_reg(), label, is_far, is_unordered);
        break;
      default:
        ShouldNotReachHere();
    }
  } else if (cmp1->is_double_fpu()) {
    assert(cmp2->is_double_fpu(), "expect double float register");
    switch (cmp_flag) {
      case lir_cond_equal:
        __ double_beq(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_notEqual:
        __ double_bne(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_less:
        __ double_blt(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_lessEqual:
        __ double_ble(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_greaterEqual:
        __ double_bge(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      case lir_cond_greater:
        __ double_bgt(cmp1->as_double_reg(), cmp2->as_double_reg(), label, is_far, is_unordered);
        break;
      default:
        ShouldNotReachHere();
    }
  } else {
    Register op1 = noreg;
    Register op2 = noreg;
    if (cmp1->is_constant()) {
      const2reg_helper(cmp1);
      op1 = t0;
    } else {
      assert(cmp1->is_single_cpu() || cmp1->is_double_cpu(), "expect single or double register");
      op1 = as_reg(cmp1);
    }
    if (cmp2->is_constant()) {
      const2reg_helper(cmp2);
      op2 = t0;
    } else {
      assert(cmp2->is_single_cpu() || cmp2->is_double_cpu(), "expect single or double register");
      op2 = as_reg(cmp2);
    }
    assert_different_registers(op1, op2);

    switch (cmp_flag) {
      case lir_cond_equal:        __ beq(op1, op2, label, is_far); break;
      case lir_cond_notEqual:     __ bne(op1, op2, label, is_far); break;
      case lir_cond_less:         __ blt(op1, op2, label, is_far); break;
      case lir_cond_lessEqual:    __ ble(op1, op2, label, is_far); break;
      case lir_cond_greaterEqual: __ bge(op1, op2, label, is_far); break;
      case lir_cond_greater:      __ bgt(op1, op2, label, is_far); break;
      case lir_cond_belowEqual:   __ bleu(op1, op2, label, is_far); break;
      case lir_cond_aboveEqual:   __ bgeu(op1, op2, label, is_far); break;
      default:                    ShouldNotReachHere();
    }
  }
}

void LIR_Assembler::emit_opBranch(LIR_OpBranch* op) {
#ifdef ASSERT
  assert(op->block() == NULL || op->block()->label() == op->label(), "wrong label");
  if (op->block() != NULL)  _branch_target_blocks.append(op->block());
  if (op->ublock() != NULL) _branch_target_blocks.append(op->ublock());
  assert(op->info() == NULL, "shouldn't have CodeEmitInfo");
#endif

  if (op->cond() == lir_cond_always) {
    if (op->info() != NULL) {
      add_debug_info_for_branch(op->info());
    }
    __ j(*(op->label()));
  } else {
    bool is_unordered = false;
    if (op->code() == lir_cond_float_branch) {
      assert(op->ublock() != NULL, "must have unordered successor");
      is_unordered = (op->ublock() == op->block());
    } else {
      assert(op->code() == lir_branch, "just checking");
    }
    emit_branch(op->cond(), op->in_opr1(), op->in_opr2(), *(op->label()), true, is_unordered);
  }
}

void LIR_Assembler::emit_opConvert(LIR_OpConvert* op) {
  LIR_Opr src  = op->in_opr();
  LIR_Opr dest = op->result_opr();

  switch (op->bytecode()) {
    case Bytecodes::_i2f:
      __ fcvt_s_w(dest->as_float_reg(), src->as_register());
      break;
    case Bytecodes::_i2d:
      __ fcvt_d_w(dest->as_double_reg(), src->as_register());
      break;
    case Bytecodes::_l2d:
      __ fcvt_d_l(dest->as_double_reg(), src->as_register_lo());
      break;
    case Bytecodes::_l2f:
      __ fcvt_s_l(dest->as_float_reg(), src->as_register_lo());
      break;
    case Bytecodes::_f2d:
      __ fcvt_d_s(dest->as_double_reg(), src->as_float_reg());
      break;
    case Bytecodes::_d2f:
      __ fcvt_s_d(dest->as_float_reg(), src->as_double_reg());
      break;
    case Bytecodes::_i2c:
      __ zero_extend(dest->as_register(), src->as_register(), 16);
      break;
    case Bytecodes::_i2l:
      __ sext_w(dest->as_register_lo(), src->as_register());
      break;
    case Bytecodes::_i2s:
      __ sign_extend(dest->as_register(), src->as_register(), 16);
      break;
    case Bytecodes::_i2b:
      __ sign_extend(dest->as_register(), src->as_register(), 8);
      break;
    case Bytecodes::_l2i:
      __ sext_w(dest->as_register(), src->as_register_lo());
      break;
    case Bytecodes::_d2l:
      __ fcvt_l_d_safe(dest->as_register_lo(), src->as_double_reg());
      break;
    case Bytecodes::_f2i:
      __ fcvt_w_s_safe(dest->as_register(), src->as_float_reg());
      break;
    case Bytecodes::_f2l:
      __ fcvt_l_s_safe(dest->as_register_lo(), src->as_float_reg());
      break;
    case Bytecodes::_d2i:
      __ fcvt_w_d_safe(dest->as_register(), src->as_double_reg());
      break;
    default: ShouldNotReachHere();
  }
}

void LIR_Assembler::emit_alloc_obj(LIR_OpAllocObj* op) {
  if (op->init_check()) {
    __ lbu(t0, Address(op->klass()->as_register(), InstanceKlass::init_state_offset()));
    __ mvw(t1, InstanceKlass::fully_initialized);
    add_debug_info_for_null_check_here(op->stub()->info());
    __ bne(t0, t1, *op->stub()->entry(), /* is_far */ true);
  }
  __ allocate_object(op->obj()->as_register(),
                     op->tmp1()->as_register(),
                     op->tmp2()->as_register(),
                     op->header_size(),
                     op->object_size(),
                     op->klass()->as_register(),
                     *op->stub()->entry());
  __ bind(*op->stub()->continuation());
}

void LIR_Assembler::emit_alloc_array(LIR_OpAllocArray* op) {
  Register len =  op->len()->as_register();
  __ sext_w(len, len);

  if (UseSlowPath ||
      (!UseFastNewObjectArray && is_reference_type(op->type())) ||
      (!UseFastNewTypeArray   && !is_reference_type(op->type()))) {
    __ j(*op->stub()->entry());
  } else {
    Register tmp1 = op->tmp1()->as_register();
    Register tmp2 = op->tmp2()->as_register();
    Register tmp3 = op->tmp3()->as_register();
    if (len == tmp1) {
      tmp1 = tmp3;
    } else if (len == tmp2) {
      tmp2 = tmp3;
    } else if (len == tmp3) {
      // everything is ok
    } else {
      __ mv(tmp3, len);
    }
    __ allocate_array(op->obj()->as_register(),
                      len,
                      tmp1,
                      tmp2,
                      arrayOopDesc::header_size(op->type()),
                      array_element_size(op->type()),
                      op->klass()->as_register(),
                      *op->stub()->entry());
  }
  __ bind(*op->stub()->continuation());
}

void LIR_Assembler::type_profile_helper(Register mdo,
                                        ciMethodData *md, ciProfileData *data,
                                        Register recv, Label* update_done) {
  for (uint i = 0; i < ReceiverTypeData::row_limit(); i++) {
    Label next_test;
    // See if the receiver is receiver[n].
    __ la(t1, Address(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_offset(i))));
    __ ld(t0, Address(t1));
    __ bne(recv, t0, next_test);
    Address data_addr(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_count_offset(i)));
    __ add_memory_int64(data_addr, DataLayout::counter_increment);
    __ j(*update_done);
    __ bind(next_test);
  }

  // Didn't find receiver; find next empty slot and fill it in
  for (uint i = 0; i < ReceiverTypeData::row_limit(); i++) {
    Label next_test;
    __ la(t1, Address(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_offset(i))));
    Address recv_addr(t1);
    __ ld(t0, recv_addr);
    __ bnez(t0, next_test);
    __ sd(recv, recv_addr);
    __ mv(t0, DataLayout::counter_increment);
    __ la(t1, Address(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_count_offset(i))));
    __ sd(t0, Address(t1));
    __ j(*update_done);
    __ bind(next_test);
  }
}

void LIR_Assembler::typecheck_helper_slowcheck(ciKlass* k, Register obj, Register Rtmp1,
                                               Register k_RInfo, Register klass_RInfo,
                                               Label* failure_target, Label* success_target) {
  // get object class
  // not a safepoint as obj null check happens earlier
  __ load_klass(klass_RInfo, obj);
  if (k->is_loaded()) {
    // See if we get an immediate positive hit
    __ ld(t0, Address(klass_RInfo, int64_t(k->super_check_offset())));
    if ((juint)in_bytes(Klass::secondary_super_cache_offset()) != k->super_check_offset()) {
      __ bne(k_RInfo, t0, *failure_target, /* is_far */ true);
      // successful cast, fall through to profile or jump
    } else {
      // See if we get an immediate positive hit
      __ beq(k_RInfo, t0, *success_target, /* is_far */ true);
      // check for self
      __ beq(klass_RInfo, k_RInfo, *success_target, /* is_far */ true);

      __ subi(sp, sp, 2 * wordSize);
      __ sd(klass_RInfo, Address(sp, wordSize));
      __ sd(k_RInfo, Address(sp, 0));
      __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::slow_subtype_check_id)));
      __ ld(k_RInfo, Address(sp, 0));
      __ addi(sp, sp, 2 * wordSize);
      // result is a boolean
      __ beqz(k_RInfo, *failure_target, /* is_far */ true);
      // successful cast, fall through to profile or jump
    }
  } else {
    // perform the fast part of the checking logic
    __ check_klass_subtype_fast_path(klass_RInfo, k_RInfo, Rtmp1, success_target, failure_target, NULL);
    // call out-of-line instance of __ check_klass_subtype_slow_path(...):
    __ subi(sp, sp, 2 * wordSize);
    __ sd(klass_RInfo, Address(sp, wordSize));
    __ sd(k_RInfo, Address(sp, 0));
    __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::slow_subtype_check_id)));
    __ ld(k_RInfo, Address(sp, 0));
    __ addi(sp, sp, 2 * wordSize);
    // result is a boolean
    __ beqz(k_RInfo, *failure_target, /* is_far */ true);
    // successful cast, fall through to profile or jump
  }
}

void LIR_Assembler::data_check(LIR_OpTypeCheck *op, ciMethodData **md, ciProfileData **data) {
  ciMethod* method = op->profiled_method();
  assert(method != NULL, "Should have method");
  int bci = op->profiled_bci();
  *md = method->method_data_or_null();
  assert(*md != NULL, "Sanity");
  *data = (*md)->bci_to_data(bci);
  assert(*data != NULL, "need data for type check");
  assert((*data)->is_ReceiverTypeData(), "need ReceiverTypeData for type check");
}

void LIR_Assembler::profile_object(ciMethodData* md, ciProfileData* data, Register obj,
                                   Register klass_RInfo, Label* obj_is_null) {
  Label not_null;
  __ bnez(obj, not_null);
  // Object is null; update MDO and exit
  Register mdo = klass_RInfo;
  __ mov_metadata(mdo, md->constant_encoding());
  Address data_addr = __ form_address(t1, mdo,
                                      md->byte_offset_of_slot(data, DataLayout::header_offset()));
  int header_bits = DataLayout::flag_mask_to_header_mask(BitData::null_seen_byte_constant());
  __ orptr(data_addr, RegisterOrConstant(header_bits), t0, t1);
  __ j(*obj_is_null);
  __ bind(not_null);
}

void LIR_Assembler::typecheck_loaded(LIR_OpTypeCheck* op, ciKlass* k, Register k_RInfo) {
  if (!k->is_loaded()) {
    klass2reg_with_patching(k_RInfo, op->info_for_patch());
  } else {
    __ mov_metadata(k_RInfo, k->constant_encoding());
  }
}

void LIR_Assembler::emit_typecheck_helper(LIR_OpTypeCheck *op, Label* success, Label* failure, Label* obj_is_null) {
  // we always need a stub for the failure case.
  CodeStub* stub = op->stub();
  Register obj = op->object()->as_register();
  Register k_RInfo = op->tmp1()->as_register();
  Register klass_RInfo = op->tmp2()->as_register();
  Register dst = op->result_opr()->as_register();
  ciKlass* k = op->klass();
  Register Rtmp1 = noreg;

  // check if it needs to be profiled
  ciMethodData* md;
  ciProfileData* data;

  const bool should_profile = op->should_profile();
  if (should_profile) {
    data_check(op, &md, &data);
  }
  Label profile_cast_success, profile_cast_failure;
  Label *success_target = should_profile ? &profile_cast_success : success;
  Label *failure_target = should_profile ? &profile_cast_failure : failure;

  if (obj == k_RInfo) {
    k_RInfo = dst;
  } else if (obj == klass_RInfo) {
    klass_RInfo = dst;
  }
  if (k->is_loaded() && !UseCompressedClassPointers) {
    select_different_registers(obj, dst, k_RInfo, klass_RInfo);
  } else {
    Rtmp1 = op->tmp3()->as_register();
    select_different_registers(obj, dst, k_RInfo, klass_RInfo, Rtmp1);
  }

  assert_different_registers(obj, k_RInfo, klass_RInfo);

  if (should_profile) {
    profile_object(md, data, obj, klass_RInfo, obj_is_null);
  } else {
    __ beqz(obj, *obj_is_null, /* is_far */ true);
  }

  typecheck_loaded(op, k, k_RInfo);
  __ verify_oop(obj);

  if (op->fast_check()) {
    // get object class
    // not a safepoint as obj null check happens earlier
    assert_different_registers(obj, k_RInfo);
    __ cmp_klass(obj, k_RInfo, t0, *success_target);
    __ j(*failure_target);
    // successful cast, jump to profile oruca
  } else {
    typecheck_helper_slowcheck(k, obj, Rtmp1, k_RInfo, klass_RInfo, failure_target, success_target);
  }
  if (should_profile) {
    type_profile(obj, md, klass_RInfo, k_RInfo, data, success, failure,
                 profile_cast_success, profile_cast_failure);
  }
  __ j(*success);
}

void LIR_Assembler::typecheck_lir_store(LIR_OpTypeCheck* op, bool should_profile) {
  Register value = op->object()->as_register();
  Register array = op->array()->as_register();
  Register k_RInfo = op->tmp1()->as_register();
  Register klass_RInfo = op->tmp2()->as_register();
  Register Rtmp1 = op->tmp3()->as_register();

  CodeStub* stub = op->stub();

  // check if it needs to be profiled
  ciMethodData* md;
  ciProfileData* data;

  if (should_profile) {
    data_check(op, &md, &data);
  }
  Label profile_cast_success, profile_cast_failure, done;
  Label *success_target = should_profile ? &profile_cast_success : &done;
  Label *failure_target = should_profile ? &profile_cast_failure : stub->entry();

  if (should_profile) {
    profile_object(md, data, value, klass_RInfo, &done);
  } else {
    __ beqz(value, done, /* is_far */ true);
  }

  add_debug_info_for_null_check_here(op->info_for_exception());
  __ load_klass(k_RInfo, array);
  __ load_klass(klass_RInfo, value);

  lir_store_slowcheck(k_RInfo, klass_RInfo, Rtmp1, success_target, failure_target);
  // fall through to the success case

  if (should_profile) {
    type_profile(value, md, klass_RInfo, k_RInfo, data, &done, stub->entry(),
                 profile_cast_success, profile_cast_failure);
  }
  __ bind(done);
}

void LIR_Assembler::type_profile(Register value, ciMethodData* md,
                                 Register klass_RInfo, Register k_RInfo,
                                 ciProfileData* data, Label* success, Label* failure,
                                 Label& profile_cast_success, Label& profile_cast_failure) {
  Register mdo  = klass_RInfo, recv = k_RInfo;
  __ bind(profile_cast_success);
  __ mov_metadata(mdo, md->constant_encoding());
  __ load_klass(recv, value);
  type_profile_helper(mdo, md, data, recv, success);
  __ j(*success);

  __ bind(profile_cast_failure);
  __ mov_metadata(mdo, md->constant_encoding());
  Address counter_addr(mdo, md->byte_offset_of_slot(data, CounterData::count_offset()));
  __ add_memory_int64(counter_addr, -DataLayout::counter_increment);
  __ j(*failure);
}

void LIR_Assembler::lir_store_slowcheck(Register k_RInfo, Register klass_RInfo, Register Rtmp1,
                                        Label* success_target, Label* failure_target) {
  // get instance klass (it's already uncompressed)
  __ ld(k_RInfo, Address(k_RInfo, ObjArrayKlass::element_klass_offset()));
  // perform the fast part of the checking logic
  __ check_klass_subtype_fast_path(klass_RInfo, k_RInfo, Rtmp1, success_target, failure_target, NULL);
  // call out-of-line instance of __ check_klass_subtype_slow_path(...):
  __ subi(sp, sp, 2 * wordSize);
  __ sd(klass_RInfo, Address(sp, wordSize));
  __ sd(k_RInfo, Address(sp, 0));
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::slow_subtype_check_id)));
  __ ld(k_RInfo, Address(sp, 0));
  __ addi(sp, sp, 2 * wordSize);
  // result is a boolean
  __ beqz(k_RInfo, *failure_target, /* is_far */ true);
}

void LIR_Assembler::emit_opTypeCheck(LIR_OpTypeCheck* op) {
  const bool should_profile = op->should_profile();

  LIR_Code code = op->code();
  if (code == lir_store_check) {
    typecheck_lir_store(op, should_profile);
  } else if (code == lir_checkcast) {
    Register obj = op->object()->as_register();
    Register dst = op->result_opr()->as_register();
    Label success;
    emit_typecheck_helper(op, &success, op->stub()->entry(), &success);
    __ bind(success);
    if (dst != obj) {
      __ mv(dst, obj);
    }
  } else if (code == lir_instanceof) {
    Register obj = op->object()->as_register();
    Register dst = op->result_opr()->as_register();
    Label success, failure, done;
    emit_typecheck_helper(op, &success, &failure, &failure);
    __ bind(failure);
    __ mv(dst, zr);
    __ j(done);
    __ bind(success);
    __ mv(dst, 1);
    __ bind(done);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::casw(Register addr, Register newval, Register cmpval) {
  __ cmpxchg(addr, cmpval, newval, Assembler::int32, Assembler::aq /* acquire */,
             Assembler::rl /* release */, t0, true /* result as bool */);
  __ seqz(t0, t0); // cmpxchg not equal, set t0 to 1
  __ membar(MacroAssembler::AnyAny);
}

void LIR_Assembler::caswu(Register addr, Register newval, Register cmpval) {
  __ cmpxchg(addr, cmpval, newval, Assembler::uint32, Assembler::aq /* acquire */,
             Assembler::rl /* release */, t0, true /* result as bool */);
  __ seqz(t0, t0); // cmpxchg not equal, set t0 to 1
  __ membar(MacroAssembler::AnyAny);
}

void LIR_Assembler::casl(Register addr, Register newval, Register cmpval) {
  __ cmpxchg(addr, cmpval, newval, Assembler::int64, Assembler::aq /* acquire */,
             Assembler::rl /* release */, t0, true /* result as bool */);
  __ seqz(t0, t0); // cmpxchg not equal, set t0 to 1
  __ membar(MacroAssembler::AnyAny);
}

void LIR_Assembler::emit_compare_and_swap(LIR_OpCompareAndSwap* op) {
  assert(VM_Version::supports_cx8(), "wrong machine");
  Register addr = as_reg(op->addr());
  Register newval = as_reg(op->new_value());
  Register cmpval = as_reg(op->cmp_value());

  if (op->code() == lir_cas_obj) {
    if (UseCompressedOops) {
      Register tmp1 = op->tmp1()->as_register();
      assert(op->tmp1()->is_valid(), "must be");
      __ encode_heap_oop(tmp1, cmpval);
      cmpval = tmp1;
      __ encode_heap_oop(t1, newval);
      newval = t1;
      caswu(addr, newval, cmpval);
    } else {
      casl(addr, newval, cmpval);
    }
  } else if (op->code() == lir_cas_int) {
    casw(addr, newval, cmpval);
  } else {
    casl(addr, newval, cmpval);
  }
}

void LIR_Assembler::cmove(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, BasicType type) {
  ShouldNotReachHere();
}

void LIR_Assembler::cmove(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, BasicType type,
                          LIR_Opr cmp_opr1, LIR_Opr cmp_opr2) {
  assert(cmp_opr1->is_valid() && cmp_opr2->is_valid(), "expect valid cmp operands for cmove on riscv");
  assert(condition != lir_cond_always, "must be");

  Label label, done;
  emit_branch(condition, cmp_opr1, cmp_opr2, label, /* is_far */ false,
              /* is_unordered */ (condition == lir_cond_greaterEqual || condition == lir_cond_greater) ? false : true);
  move_op(opr2, result, type, lir_patch_none, NULL, /*pop_fpu_stack*/false, /*unaligned*/false, /*wide*/false);
  __ j(done);
  __ bind(label);
  move_op(opr1, result, type, lir_patch_none, NULL, /*pop_fpu_stack*/false, /*unaligned*/false, /*wide*/false);
  __ bind(done);
}

void LIR_Assembler::logic_op_reg32(Register dst, Register left, Register right, LIR_Code code) {
  switch (code) {
    case lir_logic_and: __ andrw(dst, left, right); break;
    case lir_logic_or:  __ orrw (dst, left, right); break;
    case lir_logic_xor: __ xorrw(dst, left, right); break;
    default:            ShouldNotReachHere();       break;
  }
}

void LIR_Assembler::logic_op_reg(Register dst, Register left, Register right, LIR_Code code) {
  switch (code) {
    case lir_logic_and: __ andr(dst, left, right); break;
    case lir_logic_or:  __ orr (dst, left, right); break;
    case lir_logic_xor: __ xorr(dst, left, right); break;
    default:            ShouldNotReachHere();      break;
  }
}

void LIR_Assembler::logic_op_imm(Register dst, Register left, int right, LIR_Code code) {
  if (Assembler::is_simm12(right)) {
    switch (code) {
      case lir_logic_and: __ andi(dst, left, right); break;
      case lir_logic_or:  __ ori (dst, left, right); break;
      case lir_logic_xor: __ xori(dst, left, right); break;
      default: ShouldNotReachHere(); break;
    }
  } else {
    __ mv(t0, right);
    assert_different_registers(left, t0);
    logic_op_reg(dst, left, t0, code);
  }
}

void LIR_Assembler::logic_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst) {
  assert(left->is_single_cpu() || left->is_double_cpu(), "expect single or double register");
  Register Rleft = as_reg(left);
  if (dst->is_single_cpu()) {
    Register Rdst = dst->as_register();
    if (right->is_constant()) {
      logic_op_imm(Rdst, Rleft, right->as_jint(), code);
      __ sext_w(Rdst, Rdst);
    } else {
      logic_op_reg32(Rdst, Rleft, as_reg(right), code);
    }
  } else {
    Register Rdst = dst->as_register_lo();
    if (right->is_constant()) {
      logic_op_imm(Rdst, Rleft, right->as_jlong(), code);
    } else {
      logic_op_reg(Rdst, Rleft, as_reg(right), code);
    }
  }
}

void LIR_Assembler::comp_op(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Op2* op) {
  // On RISC-V, comparison operands are propagated via LIR_List::set_cmp_oprs
  // directly to LIR_OpBranch and LIR_Op2(lir_cmove).
}

void LIR_Assembler::comp_fl2i(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst, LIR_Op2* op) {
  if (code == lir_cmp_fd2i || code == lir_ucmp_fd2i) {
    bool is_unordered_less = (code == lir_ucmp_fd2i);
    if (left->is_single_fpu()) {
      __ float_compare(dst->as_register(), left->as_float_reg(), right->as_float_reg(), is_unordered_less ? -1 : 1);
    } else if (left->is_double_fpu()) {
      __ double_compare(dst->as_register(), left->as_double_reg(), right->as_double_reg(), is_unordered_less ? -1 : 1);
    } else {
      ShouldNotReachHere();
    }
  } else if (code == lir_cmp_l2i) {
    __ cmp_l2i(dst->as_register(), left->as_register_lo(), right->as_register_lo(), t0);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::align_call(LIR_Code code) {  }

void LIR_Assembler::call(LIR_OpJavaCall* op, relocInfo::relocType rtype) {
  address call = __ trampoline_call(Address(op->addr(), rtype));
  if (call == NULL) {
    bailout("trampoline stub overflow");
    return;
  }
  add_call_info(code_offset(), op->info());
}

void LIR_Assembler::ic_call(LIR_OpJavaCall* op) {
  address call = __ ic_call(op->addr());
  if (call == NULL) {
    bailout("trampoline stub overflow");
    return;
  }
  add_call_info(code_offset(), op->info());
}

void LIR_Assembler::vtable_call(LIR_OpJavaCall* op) {
  ShouldNotReachHere();
}

void LIR_Assembler::emit_static_call_stub() {
  address call_pc = __ pc();
  address stub = __ start_a_stub(_call_stub_size);
  if (stub == NULL) {
    bailout("static call stub overflow");
    return;
  }

  int start = __ offset();

  __ relocate(static_stub_Relocation::spec(call_pc));
  __ emit_static_call_stub();

  assert(__ offset() - start <= _call_stub_size, "stub too big");
  __ end_a_stub();
}

void LIR_Assembler::throw_op(LIR_Opr exceptionPC, LIR_Opr exceptionOop, CodeEmitInfo* info) {
  assert(exceptionOop->as_register() == x10, "must match");
  assert(exceptionPC->as_register() == x13, "must match");

  // exception object is not added to oop map by LinearScan
  // (LinearScan assumes that no oops are in fixed registers)
  info->add_register_oop(exceptionOop);
  Runtime1::StubID unwind_id;

  // get current pc information
  // pc is only needed if the method has an exception handler, the unwind code does not need it.
  int pc_for_athrow_offset = __ offset();
  InternalAddress pc_for_athrow(__ pc());
  __ la(exceptionPC->as_register(), pc_for_athrow);
  add_call_info(pc_for_athrow_offset, info); // for exception handler

  __ verify_not_null_oop(x10);
  // search an exception handler (x10: exception oop, x13: throwing pc)
  if (compilation()->has_fpu_code()) {
    unwind_id = Runtime1::handle_exception_id;
  } else {
    unwind_id = Runtime1::handle_exception_nofpu_id;
  }
  __ far_call(RuntimeAddress(Runtime1::entry_for(unwind_id)));

  // enough room for two byte trap
  __ nop();
}

void LIR_Assembler::unwind_op(LIR_Opr exceptionOop) {
  assert(exceptionOop->as_register() == x10, "must match");

  __ j(_unwind_handler_entry);
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, LIR_Opr count, LIR_Opr dest, LIR_Opr tmp) {
  Register lreg = as_reg(left);
  Register dreg = as_reg(dest);

  switch (left->type()) {
    case T_INT: {
      switch (code) {
        case lir_shl:  __ sllw(dreg, lreg, count->as_register()); break;
        case lir_shr:  __ sraw(dreg, lreg, count->as_register()); break;
        case lir_ushr: __ srlw(dreg, lreg, count->as_register()); break;
        default:
          ShouldNotReachHere();
          break;
      }
      break;
    }
    case T_LONG:
    case T_ADDRESS:
    case T_OBJECT: {
      switch (code) {
        case lir_shl:  __ sll(dreg, lreg, count->as_register()); break;
        case lir_shr:  __ sra(dreg, lreg, count->as_register()); break;
        case lir_ushr: __ srl(dreg, lreg, count->as_register()); break;
        default:
          ShouldNotReachHere();
          break;
      }
      break;
    }
    default:
      ShouldNotReachHere();
      break;
  }
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, jint count, LIR_Opr dest) {
  Register dreg = as_reg(dest);
  Register lreg = as_reg(left);

  switch (left->type()) {
    case T_INT: {
      count = count & 0x1F;  // Java spec
      switch (code) {
        case lir_shl:  __ slliw(dreg, lreg, count); break;
        case lir_shr:  __ sraiw(dreg, lreg, count); break;
        case lir_ushr: __ srliw(dreg, lreg, count); break;
        default:
          ShouldNotReachHere();
          break;
      }
      break;
    }
    case T_LONG:
    case T_ADDRESS:
    case T_OBJECT: {
      count = count & 0x3F;  // Java spec
      switch (code) {
        case lir_shl:  __ slli(dreg, lreg, count); break;
        case lir_shr:  __ srai(dreg, lreg, count); break;
        case lir_ushr: __ srli(dreg, lreg, count); break;
        default:
          ShouldNotReachHere();
          break;
      }
      break;
    }
    default:
      ShouldNotReachHere();
      break;
  }
}

void LIR_Assembler::store_parameter(Register r, int offset_from_rsp_in_words) {
  assert(offset_from_rsp_in_words >= 0, "invalid offset from rsp");
  int offset_from_rsp_in_bytes = offset_from_rsp_in_words * BytesPerWord;
  assert(offset_from_rsp_in_bytes < frame_map()->reserved_argument_area_size(), "invalid offset");
  __ sd(r, Address(sp, offset_from_rsp_in_bytes));
}

void LIR_Assembler::store_parameter(jint c, int offset_from_rsp_in_words) {
  assert(offset_from_rsp_in_words >= 0, "invalid offset from rsp");
  int offset_from_rsp_in_bytes = offset_from_rsp_in_words * BytesPerWord;
  assert(offset_from_rsp_in_bytes < frame_map()->reserved_argument_area_size(), "invalid offset");
  __ mvw(t0, c);
  __ sd(t0, Address(sp, offset_from_rsp_in_bytes));
}

void LIR_Assembler::store_parameter(jobject o, int offset_from_rsp_in_words) {
  ShouldNotReachHere();
}

void LIR_Assembler::emit_lock(LIR_OpLock* op) {
  Register obj = op->obj_opr()->as_register();  // may not be an oop
  Register hdr = op->hdr_opr()->as_register();
  Register lock = op->lock_opr()->as_register();
  if (!UseFastLocking) {
    __ j(*op->stub()->entry());
  } else if (op->code() == lir_lock) {
    Register scratch = noreg;
    if (UseBiasedLocking) {
      scratch = op->scratch_opr()->as_register();
    }
    assert(BasicLock::displaced_header_offset_in_bytes() == 0, "lock_reg must point to the displaced header");
    // add debug info for NullPointerException only if one is possible
    int null_check_offset = __ lock_object(hdr, obj, lock, scratch, *op->stub()->entry());
    if (op->info() != NULL) {
      add_debug_info_for_null_check(null_check_offset, op->info());
    }
    // done
  } else if (op->code() == lir_unlock) {
    assert(BasicLock::displaced_header_offset_in_bytes() == 0, "lock_reg must point to the displaced header");
    __ unlock_object(hdr, obj, lock, *op->stub()->entry());
  } else {
    Unimplemented();
  }
  __ bind(*op->stub()->continuation());
}

void LIR_Assembler::emit_profile_call(LIR_OpProfileCall* op) {
  ciMethod* method = op->profiled_method();
  int bci          = op->profiled_bci();
  ciMethod* callee = op->profiled_callee();

  // Update counter for all call types
  ciMethodData* md = method->method_data_or_null();
  assert(md != NULL, "Sanity");
  ciProfileData* data = md->bci_to_data(bci);
  assert(data != NULL && data->is_CounterData(), "need CounterData for calls");
  assert(op->mdo()->is_single_cpu(),  "mdo must be allocated");
  Register mdo  = op->mdo()->as_register();
  __ mov_metadata(mdo, md->constant_encoding());
  Address counter_addr(mdo, md->byte_offset_of_slot(data, CounterData::count_offset()));
  Bytecodes::Code bc = method->java_code_at_bci(bci);
  const bool callee_is_static = callee->is_loaded() && callee->is_static();
  // Perform additional virtual call profiling for invokevirtual and
  // invokeinterface bytecodes
  if ((bc == Bytecodes::_invokevirtual || bc == Bytecodes::_invokeinterface) &&
      !callee_is_static &&
      C1ProfileVirtualCalls) {
    assert(op->recv()->is_single_cpu(), "recv must be allocated");
    Register recv = op->recv()->as_register();
    assert_different_registers(mdo, recv);
    assert(data->is_VirtualCallData(), "need VirtualCallData for virtual calls");
    ciKlass* known_klass = op->known_holder();
    if (C1OptimizeVirtualCallProfiling && known_klass != NULL) {
      // We know the type that will be seen at this call site; we can
      // statically update the MethodData* rather than needing to do
      // dynamic tests on the receiver type

      ciVirtualCallData* vc_data = (ciVirtualCallData*) data;
      uint i;
      for (i = 0; i < VirtualCallData::row_limit(); i++) {
        ciKlass* receiver = vc_data->receiver(i);
        if (known_klass->equals(receiver)) {
          Address data_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
          __ add_memory_int64(data_addr, DataLayout::counter_increment);
          return;
        }
      }

      // Receiver type not found in profile data; select an empty slot
      for (i = 0; i < VirtualCallData::row_limit(); i++) {
        ciKlass* receiver = vc_data->receiver(i);
        if (receiver == NULL) {
          Address recv_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_offset(i)));
          __ mov_metadata(t0, known_klass->constant_encoding());
          __ la(t1, recv_addr);
          __ sd(t0, Address(t1));
          Address data_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
          __ add_memory_int64(data_addr, DataLayout::counter_increment);
          return;
        }
      }
    } else {
      __ load_klass(recv, recv);
      Label update_done;
      type_profile_helper(mdo, md, data, recv, &update_done);
      // Receiver did not match any saved receiver and there is no empty row for it.
      // Increment total counter to indicate polymorphic case.
      __ add_memory_int64(counter_addr, DataLayout::counter_increment);

      __ bind(update_done);
    }
  } else {
    // Static call
    __ add_memory_int64(counter_addr, DataLayout::counter_increment);
  }
}

void LIR_Assembler::emit_delay(LIR_OpDelay*) {
  Unimplemented();
}

void LIR_Assembler::monitor_address(int monitor_no, LIR_Opr dst) {
  __ la(dst->as_register(), frame_map()->address_for_monitor_lock(monitor_no));
}

void LIR_Assembler::emit_updatecrc32(LIR_OpUpdateCRC32* op) {
  assert(op->crc()->is_single_cpu(),  "crc must be register");
  assert(op->val()->is_single_cpu(),  "byte value must be register");
  assert(op->result_opr()->is_single_cpu(), "result must be register");
  Register crc = op->crc()->as_register();
  Register val = op->val()->as_register();
  Register res = op->result_opr()->as_register();

  assert_different_registers(val, crc, res);
  int32_t offset = 0;
  __ la_patchable(res, ExternalAddress(StubRoutines::crc_table_addr()), offset);
  __ addi(res, res, offset);

  __ notr(crc, crc); // ~crc
  __ sext_w(crc, crc);
  // update_byte_crc32:
  // tmp = (crc ^ val) & 0xff; crc = (crc >>> 8) ^ table[tmp]
  __ xorr(t0, val, crc);
  __ andi(t0, t0, 0xff);
  __ slli(t0, t0, 2);
  __ add(t0, res, t0);
  __ lw(t0, Address(t0, 0));
  __ srliw(crc, crc, 8);
  __ xorr(crc, crc, t0);
  __ notr(res, crc); // ~crc
  __ sext_w(res, res);
}

void LIR_Assembler::check_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp,
                                   Label &next, Label &none, Address mdo_addr) {
  if (exact_klass == NULL || TypeEntries::is_type_none(current_klass)) {
    if (exact_klass != NULL) {
      __ mov_metadata(tmp, exact_klass->constant_encoding());
    } else {
      __ load_klass(tmp, tmp);
    }

    __ ld(t1, mdo_addr);
    __ xorr(tmp, tmp, t1);
    __ andi(t0, tmp, TypeEntries::type_klass_mask);
    // klass seen before, nothing to do. The unknown bit may have been
    // set already but no need to check.
    __ beqz(t0, next);

    __ andi(t0, tmp, TypeEntries::type_unknown);
    __ bnez(t0, next); // already unknown. Nothing to do anymore.

    if (TypeEntries::is_type_none(current_klass)) {
      __ beqz(t1, none);
      __ mv(t0, TypeEntries::null_seen);
      __ beq(t1, t0, none);
      // There is a chance that the checks above (re-reading profiling
      // data from memory) fail if another thread has just set the
      // profiling to this obj's klass
      __ membar(MacroAssembler::LoadLoad);
      __ ld(t1, mdo_addr);
      __ xorr(tmp, tmp, t1);
      __ andi(t0, tmp, TypeEntries::type_klass_mask);
      __ beqz(t0, next);
    }
  } else {
    assert(ciTypeEntries::valid_ciklass(current_klass) != NULL &&
           ciTypeEntries::valid_ciklass(current_klass) != exact_klass, "conflict only");

    __ ld(tmp, mdo_addr);
    __ andi(t0, tmp, TypeEntries::type_unknown);
    __ bnez(t0, next); // already unknown. Nothing to do anymore.
  }

  // different than before. Cannot keep accurate profile.
  __ orptr(mdo_addr, RegisterOrConstant(TypeEntries::type_unknown), t1, t0);

  if (TypeEntries::is_type_none(current_klass)) {
    __ j(next);

    __ bind(none);
    // first time here. Set profile type.
    __ sd(tmp, mdo_addr);
#ifdef ASSERT
    __ andi(t0, tmp, TypeEntries::type_mask);
    __ verify_klass_ptr(t0);
#endif
  }
}

void LIR_Assembler::check_no_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp,
                                      Address mdo_addr, Label &next) {
  // There's a single possible klass at this profile point
  assert(exact_klass != NULL, "should be");
  if (TypeEntries::is_type_none(current_klass)) {
    __ mov_metadata(tmp, exact_klass->constant_encoding());
    __ ld(t1, mdo_addr);
    __ xorr(tmp, tmp, t1);
    __ andi(t0, tmp, TypeEntries::type_klass_mask);
    __ beqz(t0, next);
#ifdef ASSERT
    {
      Label ok;
      __ ld(t0, mdo_addr);
      __ beqz(t0, ok);
      __ mv(t1, TypeEntries::null_seen);
      __ beq(t0, t1, ok);
      // may have been set by another thread
      __ membar(MacroAssembler::LoadLoad);
      __ mov_metadata(t0, exact_klass->constant_encoding());
      __ ld(t1, mdo_addr);
      __ xorr(t1, t0, t1);
      __ andi(t1, t1, TypeEntries::type_mask);
      __ beqz(t1, ok);

      __ stop("unexpected profiling mismatch");
      __ bind(ok);
    }
#endif
    // first time here. Set profile type.
    __ sd(tmp, mdo_addr);
#ifdef ASSERT
    __ andi(t0, tmp, TypeEntries::type_mask);
    __ verify_klass_ptr(t0);
#endif
  } else {
    assert(ciTypeEntries::valid_ciklass(current_klass) != NULL &&
           ciTypeEntries::valid_ciklass(current_klass) != exact_klass, "inconsistent");

    __ ld(tmp, mdo_addr);
    __ andi(t0, tmp, TypeEntries::type_unknown);
    __ bnez(t0, next); // already unknown. Nothing to do anymore.

    __ ori(tmp, tmp, TypeEntries::type_unknown);
    __ sd(tmp, mdo_addr);
  }
}

void LIR_Assembler::check_exact_klass(Register tmp, ciKlass* exact_klass) {
#ifdef ASSERT
  if (exact_klass != NULL) {
    Label ok;
    __ load_klass(tmp, tmp);
    __ mov_metadata(t0, exact_klass->constant_encoding());
    __ beq(tmp, t0, ok);
    __ stop("exact klass and actual klass differ");
    __ bind(ok);
  }
#endif
}

void LIR_Assembler::check_null(Register tmp, Label &update, intptr_t current_klass,
                               Address mdo_addr, bool do_update, Label &next) {
  __ bnez(tmp, update);
  if (!TypeEntries::was_null_seen(current_klass)) {
    __ orptr(mdo_addr, RegisterOrConstant(TypeEntries::null_seen), t1, t0);
  }
  if (do_update) {
    __ j(next);
  }
}

void LIR_Assembler::emit_profile_type(LIR_OpProfileType* op) {
  COMMENT("emit_profile_type {");
  Register obj = op->obj()->as_register();
  Register tmp = op->tmp()->as_pointer_register();
  Address mdo_addr = as_Address(op->mdp()->as_address_ptr());
  ciKlass* exact_klass = op->exact_klass();
  intptr_t current_klass = op->current_klass();
  bool not_null = op->not_null();
  bool no_conflict = op->no_conflict();

  Label update, next, none;

  bool do_null = !not_null;
  bool exact_klass_set = exact_klass != NULL && ciTypeEntries::valid_ciklass(current_klass) == exact_klass;
  bool do_update = !TypeEntries::is_type_unknown(current_klass) && !exact_klass_set;

  assert(do_null || do_update, "why are we here?");
  assert(!TypeEntries::was_null_seen(current_klass) || do_update, "why are we here?");
  assert(mdo_addr.base() != t0, "wrong register");

  __ verify_oop(obj);

  if (tmp != obj) {
    __ mv(tmp, obj);
  }
  if (do_null) {
    check_null(tmp, update, current_klass, mdo_addr, do_update, next);
  } else {
#ifdef ASSERT
    __ bnez(tmp, update);
    __ stop("unexpected null obj");
#endif
  }

  __ bind(update);

  if (do_update) {
    check_exact_klass(tmp, exact_klass);
    if (!no_conflict) {
      check_conflict(exact_klass, current_klass, tmp, next, none, mdo_addr);
    } else {
      check_no_conflict(exact_klass, current_klass, tmp, mdo_addr, next);
    }

    __ bind(next);
  }
  COMMENT("} emit_profile_type");
}

void LIR_Assembler::align_backward_branch_target() {
}

void LIR_Assembler::negate(LIR_Opr left, LIR_Opr dest) {
  if (left->is_single_cpu()) {
    assert(dest->is_single_cpu(), "expect single result reg");
    __ negw(dest->as_register(), left->as_register());
  } else if (left->is_double_cpu()) {
    assert(dest->is_double_cpu(), "expect double result reg");
    __ neg(dest->as_register_lo(), left->as_register_lo());
  } else if (left->is_single_fpu()) {
    assert(dest->is_single_fpu(), "expect single float result reg");
    __ fneg_s(dest->as_float_reg(), left->as_float_reg());
  } else {
    assert(left->is_double_fpu(), "expect double float operand reg");
    assert(dest->is_double_fpu(), "expect double float result reg");
    __ fneg_d(dest->as_double_reg(), left->as_double_reg());
  }
}

void LIR_Assembler::leal(LIR_Opr addr, LIR_Opr dest) {
  __ la(dest->as_register_lo(), as_Address(addr->as_address_ptr()));
}

void LIR_Assembler::rt_call(LIR_Opr result, address dest, const LIR_OprList* args, LIR_Opr tmp, CodeEmitInfo* info) {
  assert(!tmp->is_valid(), "don't need temporary");

  CodeBlob* cb = CodeCache::find_blob(dest);
  if (cb != NULL) {
    __ far_call(RuntimeAddress(dest));
  } else {
    __ mv(t0, RuntimeAddress(dest));
    __ jalr(t0);
  }

  if (info != NULL) {
    add_call_info_here(info);
  }
}

void LIR_Assembler::volatile_move_op(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info) {
  if (dest->is_address() || src->is_address()) {
    move_op(src, dest, type, lir_patch_none, info,
            /*pop_fpu_stack*/false, /*unaligned*/false, /*wide*/false);
  } else {
    ShouldNotReachHere();
  }
}

#ifdef ASSERT
// emit run-time assertion
void LIR_Assembler::emit_assert(LIR_OpAssert* op) {
  assert(op->code() == lir_assert, "must be");

  Label ok;
  if (op->in_opr1()->is_valid()) {
    assert(op->in_opr2()->is_valid(), "both operands must be valid");
    assert(op->condition() != lir_cond_always, "no other conditions allowed");
    emit_branch(op->condition(), op->in_opr1(), op->in_opr2(), ok, /* is_far */ true, /* is_unordered */ false);
  } else {
    assert(op->in_opr2()->is_illegal(), "both operands must be illegal");
    assert(op->condition() == lir_cond_always, "no other conditions allowed");
  }

  if (op->halt()) {
    const char* str = __ code_string(op->msg());
    __ stop(str);
  } else {
    breakpoint();
  }
  __ bind(ok);
}
#endif // ASSERT

void LIR_Assembler::membar() {
  COMMENT("membar");
  __ membar(MacroAssembler::AnyAny);
}

void LIR_Assembler::membar_acquire() {
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
}

void LIR_Assembler::membar_release() {
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
}

void LIR_Assembler::membar_loadload() {
  __ membar(MacroAssembler::LoadLoad);
}

void LIR_Assembler::membar_storestore() {
  __ membar(MacroAssembler::StoreStore);
}

void LIR_Assembler::membar_loadstore() { __ membar(MacroAssembler::LoadStore); }

void LIR_Assembler::membar_storeload() { __ membar(MacroAssembler::StoreLoad); }

void LIR_Assembler::get_thread(LIR_Opr result_reg) {
  __ mv(result_reg->as_register(), xthread);
}

void LIR_Assembler::peephole(LIR_List *lir) {
}

void LIR_Assembler::get_op(BasicType type) {
  switch (type) {
    case T_INT:
      xchg = &MacroAssembler::atomic_xchgalw;
      add = &MacroAssembler::atomic_addalw;
      break;
    case T_LONG:
      xchg = &MacroAssembler::atomic_xchgal;
      add = &MacroAssembler::atomic_addal;
      break;
    case T_OBJECT:
    case T_ARRAY:
      if (UseCompressedOops) {
        xchg = &MacroAssembler::atomic_xchgalwu;
        add = &MacroAssembler::atomic_addalw;
      } else {
        xchg = &MacroAssembler::atomic_xchgal;
        add = &MacroAssembler::atomic_addal;
      }
      break;
    default:
      ShouldNotReachHere();
      xchg = &MacroAssembler::atomic_xchgal;
      add = &MacroAssembler::atomic_addal; // unreachable
  }
}

void LIR_Assembler::atomic_op(LIR_Code code, LIR_Opr src, LIR_Opr data, LIR_Opr dest, LIR_Opr tmp_op) {
  Address addr = as_Address(src->as_address_ptr());
  BasicType type = src->type();
  bool is_oop = is_reference_type(type);

  get_op(type);

  switch (code) {
    case lir_xadd: {
      RegisterOrConstant inc;
      Register tmp = as_reg(tmp_op);
      Register dst = as_reg(dest);
      if (data->is_constant()) {
        inc = RegisterOrConstant(as_long(data));
        assert_different_registers(dst, addr.base(), tmp, t0);
      } else {
        inc = RegisterOrConstant(as_reg(data));
        assert_different_registers(inc.as_register(), dst, addr.base(), tmp, t0);
      }
      __ la(tmp, addr);
      (_masm->*add)(dst, inc, tmp);
      break;
    }
    case lir_xchg: {
      Register tmp = tmp_op->as_register();
      Register obj = as_reg(data);
      Register dst = as_reg(dest);
      if (is_oop && UseCompressedOops) {
        __ encode_heap_oop(t0, obj);
        obj = t0;
      }
      assert_different_registers(obj, addr.base(), tmp, t1, dst);
      __ la(tmp, addr);
      (_masm->*xchg)(dst, obj, tmp);
      if (is_oop && UseCompressedOops) {
        __ decode_heap_oop(dst);
      }
      break;
    }
    default:
      ShouldNotReachHere();
  }
}

// ----------------------------------------------------------------------------
// Arithmetic Operations (ported from JDK 19 c1_LIRAssembler_arith_riscv.cpp)
// ----------------------------------------------------------------------------

void LIR_Assembler::arithmetic_idiv(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr temp, LIR_Opr result, CodeEmitInfo* info) {
  assert((code == lir_idiv) || (code == lir_irem), "opcode must be idiv or irem");
  bool is_irem = (code == lir_irem);
  assert(left->is_single_cpu(), "left must be a register");
  assert(right->is_single_cpu() || right->is_constant(), "right must be a register or constant");
  assert(result->is_single_cpu(), "result must be a register");
  Register lreg = left->as_register();
  Register dreg = result->as_register();

  if (right->is_constant()) {
    int c = right->as_constant_ptr()->as_jint();
    assert(c > 0 && is_power_of_2(c), "divisor must be power-of-2 constant");
    if (is_irem) {
      if (c == 1) {
        __ mv(dreg, zr);
      } else {
        unsigned int shift = exact_log2(c);
        __ sraiw(t0, lreg, 0x1f);
        __ srliw(t0, t0, BitsPerInt - shift);
        __ addw(t1, lreg, t0);
        if (Assembler::is_simm12(c - 1)) {
          __ andi(t1, t1, c - 1);
        } else {
          __ zext(t1, t1, shift);
        }
        __ subw(dreg, t1, t0);
      }
    } else {
      if (c == 1) {
        __ mv(dreg, lreg);
      } else {
        unsigned int shift = exact_log2(c);
        __ sraiw(t0, lreg, 0x1f);
        if (Assembler::is_simm12(c - 1)) {
          __ andi(t0, t0, c - 1);
        } else {
          __ zext(t0, t0, shift);
        }
        __ addw(dreg, t0, lreg);
        __ sraiw(dreg, dreg, shift);
      }
    }
  } else {
    Register rreg = right->as_register();
    __ corrected_idivl(dreg, lreg, rreg, is_irem);
  }
}

void LIR_Assembler::arithmetic_idiv(LIR_Op3* op, bool is_irem) {
  arithmetic_idiv(op->code(), op->in_opr1(), op->in_opr2(), op->in_opr3(), op->result_opr(), op->info());
}

void LIR_Assembler::arith_op_single_cpu_right_constant(LIR_Code code, LIR_Opr left, LIR_Opr right,
                                                       Register lreg, Register dreg) {
  // cpu register - constant
  jlong c;

  switch (right->type()) {
    case T_LONG:
      c = right->as_constant_ptr()->as_jlong();
      break;
    case T_INT:
    case T_ADDRESS:
      c = right->as_constant_ptr()->as_jint();
      break;
    default:
      ShouldNotReachHere();
      c = 0; // unreachable
      break;
  }

  assert(code == lir_add || code == lir_sub, "mismatched arithmetic op");
  if (c == 0 && dreg == lreg) {
    COMMENT("effective nop elided");
    return;
  }
  switch (left->type()) {
    case T_INT:
      switch (code) {
        case lir_add: __ addw(dreg, lreg, c); break;
        case lir_sub: __ subw(dreg, lreg, c); break;
        default: ShouldNotReachHere();
      }
    break;
    case T_OBJECT:
    case T_ADDRESS:
      switch (code) {
        case lir_add: __ add(dreg, lreg, c); break;
        case lir_sub: __ sub(dreg, lreg, c); break;
        default: ShouldNotReachHere();
      }
      break;
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_single_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Register lreg = left->as_register();
  Register dreg = as_reg(dest);

  if (right->is_single_cpu()) {
    // cpu register - cpu register
    assert(left->type() == T_INT && right->type() == T_INT && dest->type() == T_INT, "should be");
    Register rreg = right->as_register();
    switch (code) {
      case lir_add: __ addw(dest->as_register(), lreg, rreg); break;
      case lir_sub: __ subw(dest->as_register(), lreg, rreg); break;
      case lir_mul: __ mulw(dest->as_register(), lreg, rreg); break;
      default:      ShouldNotReachHere();
    }
  } else if (right->is_double_cpu()) {
    Register rreg = right->as_register_lo();
    // single_cpu + double_cpu: can happen with obj+long
    assert(code == lir_add || code == lir_sub, "mismatched arithmetic op");
    switch (code) {
      case lir_add: __ add(dreg, lreg, rreg); break;
      case lir_sub: __ sub(dreg, lreg, rreg); break;
      default: ShouldNotReachHere();
    }
  } else if (right->is_constant()) {
    arith_op_single_cpu_right_constant(code, left, right, lreg, dreg);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_double_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Register lreg_lo = left->as_register_lo();

  if (right->is_double_cpu()) {
    // cpu register - cpu register
    Register rreg_lo = right->as_register_lo();
    switch (code) {
      case lir_add: __ add(dest->as_register_lo(), lreg_lo, rreg_lo); break;
      case lir_sub: __ sub(dest->as_register_lo(), lreg_lo, rreg_lo); break;
      case lir_mul: __ mul(dest->as_register_lo(), lreg_lo, rreg_lo); break;
      case lir_div: __ corrected_idivq(dest->as_register_lo(), lreg_lo, rreg_lo, false); break;
      case lir_rem: __ corrected_idivq(dest->as_register_lo(), lreg_lo, rreg_lo, true); break;
      default:
        ShouldNotReachHere();
    }

  } else if (right->is_constant()) {
    jlong c = right->as_constant_ptr()->as_jlong();
    Register dreg = as_reg(dest);
    switch (code) {
      case lir_add: // fall through
      case lir_sub:
        if (c == 0 && dreg == lreg_lo) {
          COMMENT("effective nop elided");
          return;
        }
        code == lir_add ? __ add(dreg, lreg_lo, c) : __ sub(dreg, lreg_lo, c);
        break;
      case lir_div:
        assert(c > 0 && is_power_of_2_long(c), "divisor must be power-of-2 constant");
        if (c == 1) {
          // move lreg_lo to dreg if divisor is 1
          __ mv(dreg, lreg_lo);
        } else {
          unsigned int shift = exact_log2_long(c);
          // use t0 as intermediate result register
          __ srai(t0, lreg_lo, 0x3f);
          if (Assembler::is_simm12(c - 1)) {
            __ andi(t0, t0, c - 1);
          } else {
            __ zext(t0, t0, shift);
          }
          __ add(dreg, t0, lreg_lo);
          __ srai(dreg, dreg, shift);
        }
        break;
      case lir_rem:
        assert(c > 0 && is_power_of_2_long(c), "divisor must be power-of-2 constant");
        if (c == 1) {
          // move 0 to dreg if divisor is 1
          __ mv(dreg, zr);
        } else {
          unsigned int shift = exact_log2_long(c);
          __ srai(t0, lreg_lo, 0x3f);
          __ srli(t0, t0, BitsPerLong - shift);
          __ add(t1, lreg_lo, t0);
          if (Assembler::is_simm12(c - 1)) {
            __ andi(t1, t1, c - 1);
          } else {
            __ zext(t1, t1, shift);
          }
          __ sub(dreg, t1, t0);
        }
        break;
      default:
        ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_single_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  assert(right->is_single_fpu(), "right hand side of float arithmetics needs to be float register");
  switch (code) {
    case lir_add: __ fadd_s(dest->as_float_reg(), left->as_float_reg(), right->as_float_reg()); break;
    case lir_sub: __ fsub_s(dest->as_float_reg(), left->as_float_reg(), right->as_float_reg()); break;
    case lir_mul_strictfp: // fall through
    case lir_mul: __ fmul_s(dest->as_float_reg(), left->as_float_reg(), right->as_float_reg()); break;
    case lir_div_strictfp: // fall through
    case lir_div: __ fdiv_s(dest->as_float_reg(), left->as_float_reg(), right->as_float_reg()); break;
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_double_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  if (right->is_double_fpu()) {
    // fpu register - fpu register
    switch (code) {
      case lir_add: __ fadd_d(dest->as_double_reg(), left->as_double_reg(), right->as_double_reg()); break;
      case lir_sub: __ fsub_d(dest->as_double_reg(), left->as_double_reg(), right->as_double_reg()); break;
      case lir_mul_strictfp: // fall through
      case lir_mul: __ fmul_d(dest->as_double_reg(), left->as_double_reg(), right->as_double_reg()); break;
      case lir_div_strictfp: // fall through
      case lir_div: __ fdiv_d(dest->as_double_reg(), left->as_double_reg(), right->as_double_reg()); break;
      default:
        ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest,
                             CodeEmitInfo* info, bool pop_fpu_stack) {
  assert(info == NULL, "should never be used, idiv/irem and ldiv/lrem not handled by this method");

  if (left->is_single_cpu()) {
    arith_op_single_cpu(code, left, right, dest);
  } else if (left->is_double_cpu()) {
    arith_op_double_cpu(code, left, right, dest);
  } else if (left->is_single_fpu()) {
    arith_op_single_fpu(code, left, right, dest);
  } else if (left->is_double_fpu()) {
    arith_op_double_fpu(code, left, right, dest);
  } else if (left->is_single_stack() || left->is_address()) {
    assert(left == dest, "left and dest must be equal");
    ShouldNotReachHere();
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_fpu_implementation(LIR_Code code, int left_index, int right_index, int dest_index, bool pop_fpu_stack) {
  Unimplemented();
}

void LIR_Assembler::intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op) {
  switch (code) {
    case lir_abs : __ fabs_d(dest->as_double_reg(), value->as_double_reg()); break;
    case lir_sqrt: __ fsqrt_d(dest->as_double_reg(), value->as_double_reg()); break;
    default      : ShouldNotReachHere();
  }
}

// ----------------------------------------------------------------------------
// Arraycopy Operations (ported from JDK 19 c1_LIRAssembler_arraycopy_riscv.cpp)
// ----------------------------------------------------------------------------

#define PUSH(reg1, reg2)          \
  __ subi(sp, sp, 16);            \
  __ sd(reg1, Address(sp, 8));    \
  __ sd(reg2, Address(sp, 0));

#define POP(reg1, reg2)           \
  __ ld(reg1, Address(sp, 8));    \
  __ ld(reg2, Address(sp, 0));    \
  __ addi(sp, sp, 16);

void LIR_Assembler::arraycopy_store_args(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos) {
  __ sd(dst, Address(sp, 0 * BytesPerWord));
  __ sd(dst_pos, Address(sp, 1 * BytesPerWord));
  __ sd(length, Address(sp, 2 * BytesPerWord));
  __ sd(src_pos, Address(sp, 3 * BytesPerWord));
  __ sd(src, Address(sp, 4 * BytesPerWord));
}

void LIR_Assembler::arraycopy_load_args(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos) {
  __ ld(dst, Address(sp, 0 * BytesPerWord));
  __ ld(dst_pos, Address(sp, 1 * BytesPerWord));
  __ ld(length, Address(sp, 2 * BytesPerWord));
  __ ld(src_pos, Address(sp, 3 * BytesPerWord));
  __ ld(src, Address(sp, 4 * BytesPerWord));
}

void LIR_Assembler::generic_arraycopy(Register src, Register src_pos, Register length,
                                      Register dst, Register dst_pos, CodeStub *stub) {
  assert(src == x11 && src_pos == x12, "mismatch in calling convention");

  // Save the arguments in case the generic arraycopy fails and we
  // have to fall back to the JNI stub
  arraycopy_store_args(src, src_pos, length, dst, dst_pos);

  address C_entry = CAST_FROM_FN_PTR(address, Runtime1::arraycopy);
  address copyfunc_addr = StubRoutines::generic_arraycopy();
  // The arguments are in java calling convention so we shift them
  // to C convention
  assert_different_registers(c_rarg0, j_rarg1, j_rarg2, j_rarg3, j_rarg4);
  __ mv(c_rarg0, j_rarg0);
  assert_different_registers(c_rarg1, j_rarg2, j_rarg3, j_rarg4);
  __ mv(c_rarg1, j_rarg1);
  assert_different_registers(c_rarg2, j_rarg3, j_rarg4);
  __ mv(c_rarg2, j_rarg2);
  assert_different_registers(c_rarg3, j_rarg4);
  __ mv(c_rarg3, j_rarg3);
  __ mv(c_rarg4, j_rarg4);
  if (copyfunc_addr == NULL) { // Use C version if stub was not generated
    __ mv(t0, RuntimeAddress(C_entry));
    __ jalr(t0);
  } else {
#ifndef PRODUCT
    if (PrintC1Statistics) {
      __ add_memory_int32(ExternalAddress((address)&Runtime1::_generic_arraycopystub_cnt), 1);
    }
#endif
    __ far_call(RuntimeAddress(copyfunc_addr));
  }

  __ beqz(x10, *stub->continuation(), /* is_far */ true);

  // Reload values from the stack so they are where the stub
  // expects them.
  arraycopy_load_args(src, src_pos, length, dst, dst_pos);

  if (copyfunc_addr != NULL) {
    // x10 is -1^K where K == partial copied count
    __ notr(t0, x10);
    // adjust length down and src/end pos up by partial copied count
    __ subw(length, length, t0);
    __ addw(src_pos, src_pos, t0);
    __ addw(dst_pos, dst_pos, t0);
  }
  __ j(*stub->entry());

  __ bind(*stub->continuation());
}

void LIR_Assembler::arraycopy_simple_check(Register src, Register src_pos, Register length,
                                           Register dst, Register dst_pos, Register tmp,
                                           CodeStub *stub, int flags) {
  // test for NULL
  if (flags & LIR_OpArrayCopy::src_null_check) {
    __ beqz(src, *stub->entry(), /* is_far */ true);
  }
  if (flags & LIR_OpArrayCopy::dst_null_check) {
    __ beqz(dst, *stub->entry(), /* is_far */ true);
  }

  // If the compiler was not able to prove that exact type of the source or the destination
  // of the arraycopy is an array type, check at runtime if the source or the destination is
  // an instance type.
  if (flags & LIR_OpArrayCopy::type_check) {
    if (!(flags & LIR_OpArrayCopy::LIR_OpArrayCopy::dst_objarray)) {
      __ load_klass(tmp, dst);
      __ lw(t0, Address(tmp, in_bytes(Klass::layout_helper_offset())));
      __ mvw(t1, Klass::_lh_neutral_value);
      __ bge(t0, t1, *stub->entry(), /* is_far */ true);
    }
    if (!(flags & LIR_OpArrayCopy::LIR_OpArrayCopy::src_objarray)) {
      __ load_klass(tmp, src);
      __ lw(t0, Address(tmp, in_bytes(Klass::layout_helper_offset())));
      __ mvw(t1, Klass::_lh_neutral_value);
      __ bge(t0, t1, *stub->entry(), /* is_far */ true);
    }
  }

  // check if negative
  if (flags & LIR_OpArrayCopy::src_pos_positive_check) {
    __ sext_w(t0, src_pos);
    __ bltz(t0, *stub->entry(), /* is_far */ true);
  }
  if (flags & LIR_OpArrayCopy::dst_pos_positive_check) {
    __ sext_w(t0, dst_pos);
    __ bltz(t0, *stub->entry(), /* is_far */ true);
  }

  if (flags & LIR_OpArrayCopy::length_positive_check) {
    __ sext_w(t0, length);
    __ bltz(t0, *stub->entry(), /* is_far */ true);
  }

  if (flags & LIR_OpArrayCopy::src_range_check) {
    __ lwu(tmp, Address(src, arrayOopDesc::length_offset_in_bytes()));
    __ addw(t0, length, src_pos);
    __ bltu(tmp, t0, *stub->entry(), /* is_far */ true);
  }
  if (flags & LIR_OpArrayCopy::dst_range_check) {
    __ lwu(tmp, Address(dst, arrayOopDesc::length_offset_in_bytes()));
    __ addw(t0, length, dst_pos);
    __ bltu(tmp, t0, *stub->entry(), /* is_far */ true);
  }

  // In JDK 8u, StubRoutines::*_arraycopy stubs require count != 0 (whereas
  // LIRGenerator::arraycopy_helper may clear length_positive_check if length
  // comes from an arraylength). Always skip the copy when length == 0,
  // matching AArch64 JDK 8u (c1_LIRAssembler_aarch64.cpp:2381).
  __ sext_w(t0, length);
  __ beqz(t0, *stub->continuation(), /* is_far */ true);
}

void LIR_Assembler::arraycopy_checkcast_prepare_params(Register src, Register src_pos, Register length,
                                                       Register dst, Register dst_pos, BasicType basic_type) {
  int scale = exact_log2(type2aelembytes(basic_type));
  __ zext_w(t0, src_pos);
  __ shadd(c_rarg0, t0, src, t0, scale);
  __ addi(c_rarg0, c_rarg0, arrayOopDesc::base_offset_in_bytes(basic_type));
  assert_different_registers(c_rarg0, dst, dst_pos, length);
  __ zext_w(t0, dst_pos);
  __ shadd(c_rarg1, t0, dst, t0, scale);
  __ addi(c_rarg1, c_rarg1, arrayOopDesc::base_offset_in_bytes(basic_type));
  assert_different_registers(c_rarg1, dst, length);
  __ zext_w(c_rarg2, length);
  assert_different_registers(c_rarg2, dst);
}

void LIR_Assembler::arraycopy_checkcast(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos, Register tmp,
                                        CodeStub *stub, BasicType basic_type,
                                        address copyfunc_addr, int flags) {
  // Stub is not generated if AvoidObjectCopy is true
  if (copyfunc_addr != NULL) {
    // src is not a sub class of dst so we have to do a
    // per-element check.
    int mask = LIR_OpArrayCopy::src_objarray|LIR_OpArrayCopy::dst_objarray;
    if ((flags & mask) != mask) {
      // Check that at least both of them object arrays.
      assert(flags & mask, "one of the two should be known to be an object array");

      if (!(flags & LIR_OpArrayCopy::src_objarray)) {
        __ load_klass(tmp, src);
      } else if (!(flags & LIR_OpArrayCopy::dst_objarray)) {
        __ load_klass(tmp, dst);
      }
      int lh_offset = in_bytes(Klass::layout_helper_offset());
      Address klass_lh_addr(tmp, lh_offset);
      jint objArray_lh = Klass::array_layout_helper(T_OBJECT);
      __ lw(t0, klass_lh_addr);
      __ mvw(t1, objArray_lh);
      __ bne(t0, t1, *stub->entry(), /* is_far */ true);
    }

    // Spill because stubs can use any register they like and it's
    // easier to restore just those that we care about.
    arraycopy_store_args(src, src_pos, length, dst, dst_pos);

    arraycopy_checkcast_prepare_params(src, src_pos, length, dst, dst_pos, basic_type);

    __ load_klass(c_rarg4, dst);
    __ ld(c_rarg4, Address(c_rarg4, ObjArrayKlass::element_klass_offset()));
    __ lwu(c_rarg3, Address(c_rarg4, Klass::super_check_offset_offset()));
    __ far_call(RuntimeAddress(copyfunc_addr));

#ifndef PRODUCT
    if (PrintC1Statistics) {
      Label failed;
      __ bnez(x10, failed);
      __ add_memory_int32(ExternalAddress((address)&Runtime1::_arraycopy_checkcast_cnt), 1);
      __ bind(failed);
    }
#endif

    __ beqz(x10, *stub->continuation(), /* is_far */ true);

#ifndef PRODUCT
    if (PrintC1Statistics) {
      __ add_memory_int32(ExternalAddress((address)&Runtime1::_arraycopy_checkcast_attempt_cnt), 1);
    }
#endif
    assert_different_registers(dst, dst_pos, length, src_pos, src, x10, t0);

    // Restore previously spilled arguments
    arraycopy_load_args(src, src_pos, length, dst, dst_pos);

    // return value is -1^K where K is partial copied count
    __ notr(t0, x10);
    // adjust length down and src/end pos up by partial copied count
    __ subw(length, length, t0);
    __ addw(src_pos, src_pos, t0);
    __ addw(dst_pos, dst_pos, t0);
  }

  __ j(*stub->entry());
}

void LIR_Assembler::arraycopy_type_check(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos, Register tmp,
                                         CodeStub *stub, BasicType basic_type, int flags) {
  if (flags & LIR_OpArrayCopy::type_check) {
    // We don't know the array types are compatible
    if (basic_type != T_OBJECT) {
      // Simple test for basic type arrays
      if (UseCompressedClassPointers) {
        // We don't need decode because we just need to compare
        __ lwu(tmp, Address(src, oopDesc::klass_offset_in_bytes()));
        __ lwu(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      } else {
        __ ld(tmp, Address(src, oopDesc::klass_offset_in_bytes()));
        __ ld(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      }
      __ bne(tmp, t0, *stub->entry(), /* is_far */ true);
    } else {
      // For object arrays, if src is a sub class of dst then we can
      // safely do the copy.
      Label cont, slow;

      PUSH(src, dst);

      __ load_klass(src, src);
      __ load_klass(dst, dst);

      __ check_klass_subtype_fast_path(src, dst, tmp, &cont, &slow, NULL);

      PUSH(src, dst);
      __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::slow_subtype_check_id)));
      POP(src, dst);

      __ bnez(dst, cont);

      __ bind(slow);
      POP(src, dst);

      address copyfunc_addr = StubRoutines::checkcast_arraycopy();
      arraycopy_checkcast(src, src_pos, length, dst, dst_pos, tmp, stub, basic_type, copyfunc_addr, flags);

      __ bind(cont);
      POP(src, dst);
    }
  }
}

void LIR_Assembler::arraycopy_assert(Register src, Register dst, Register tmp, ciArrayKlass* default_type, int flags) {
#ifdef ASSERT
  BasicType basic_type = default_type != NULL ? default_type->element_type()->basic_type() : T_ILLEGAL;
  if (basic_type == T_ARRAY) basic_type = T_OBJECT;
  if (basic_type != T_OBJECT || !(flags & LIR_OpArrayCopy::type_check)) {
    // Sanity check the known type with the incoming class.  For the
    // primitive case the types must match exactly with src.klass and
    // dst.klass each exactly matching the default type.  For the
    // object array case, if no type check is needed then either the
    // dst type is exactly the expected type and the src type is a
    // subtype which we can't check or src is the same array as dst
    // but not necessarily exactly of type default_type.
    Label known_ok, halt;
    __ mov_metadata(tmp, default_type->constant_encoding());
    if (UseCompressedClassPointers) {
      __ encode_klass_not_null(tmp);
    }

    if (basic_type != T_OBJECT) {
      if (UseCompressedClassPointers) {
        __ lwu(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      } else {
        __ ld(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      }
      __ bne(tmp, t0, halt);
      if (UseCompressedClassPointers) {
        __ lwu(t0, Address(src, oopDesc::klass_offset_in_bytes()));
      } else {
        __ ld(t0, Address(src, oopDesc::klass_offset_in_bytes()));
      }
      __ beq(tmp, t0, known_ok);
    } else {
      if (UseCompressedClassPointers) {
        __ lwu(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      } else {
        __ ld(t0, Address(dst, oopDesc::klass_offset_in_bytes()));
      }
      __ beq(tmp, t0, known_ok);
      __ beq(src, dst, known_ok);
    }
    __ bind(halt);
    __ stop("incorrect type information in arraycopy");
    __ bind(known_ok);
  }
#endif
}

void LIR_Assembler::arraycopy_prepare_params(Register src, Register src_pos, Register length,
                                             Register dst, Register dst_pos, BasicType basic_type) {
  int scale = exact_log2(type2aelembytes(basic_type));
  // Convert 32 bits signed value into 64 bits signed value to avoid negative num of Register
  __ sext_w(t0, src_pos);
  __ shadd(c_rarg0, t0, src, t0, scale);
  __ addi(c_rarg0, c_rarg0, arrayOopDesc::base_offset_in_bytes(basic_type));
  assert_different_registers(c_rarg0, dst, dst_pos, length);
  // Convert 32 bits signed value into 64 bits signed value to avoid negative num of Register
  __ sext_w(t0, dst_pos);
  __ shadd(c_rarg1, t0, dst, t0, scale);
  __ addi(c_rarg1, c_rarg1, arrayOopDesc::base_offset_in_bytes(basic_type));
  assert_different_registers(c_rarg1, dst, length);
  // Convert 32 bits signed value into 64 bits signed value to avoid negative num of Register
  __ sext_w(c_rarg2, length);
  assert_different_registers(c_rarg2, dst);
}

// This code replaces a call to arraycopy; no exception may
// be thrown in this code, they must be thrown in the System.arraycopy
// activation frame; we could save some checks if this would not be the case
void LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy* op) {
  ciArrayKlass* default_type = op->expected_type();
  Register src = op->src()->as_register();
  Register dst = op->dst()->as_register();
  Register src_pos = op->src_pos()->as_register();
  Register dst_pos = op->dst_pos()->as_register();
  Register length  = op->length()->as_register();
  Register tmp = op->tmp()->as_register();

  CodeStub* stub = op->stub();
  int flags = op->flags();
  BasicType basic_type = default_type != NULL ? default_type->element_type()->basic_type() : T_ILLEGAL;
  if (is_reference_type(basic_type)) basic_type = T_OBJECT;

  // if we don't know anything, just go through the generic arraycopy
  if (default_type == NULL) {
    generic_arraycopy(src, src_pos, length, dst, dst_pos, stub);
    return;
  }

  assert(default_type != NULL && default_type->is_array_klass() && default_type->is_loaded(),
         "must be true at this point");

  arraycopy_simple_check(src, src_pos, length, dst, dst_pos, tmp, stub, flags);
  arraycopy_type_check(src, src_pos, length, dst, dst_pos, tmp, stub, basic_type, flags);
  arraycopy_assert(src, dst, tmp, default_type, flags);

#ifndef PRODUCT
  if (PrintC1Statistics) {
    __ add_memory_int32(ExternalAddress(Runtime1::arraycopy_count_address(basic_type)), 1);
  }
#endif

  arraycopy_prepare_params(src, src_pos, length, dst, dst_pos, basic_type);

  bool disjoint = (flags & LIR_OpArrayCopy::overlapping) == 0;
  bool aligned = (flags & LIR_OpArrayCopy::unaligned) == 0;
  const char *name;
  address entry = StubRoutines::select_arraycopy_function(basic_type, aligned, disjoint, name, false);

  CodeBlob *cb = CodeCache::find_blob(entry);
  if (cb != NULL) {
    __ far_call(RuntimeAddress(entry));
  } else {
    __ call_VM_leaf(entry, 3);
  }

  __ bind(*stub->continuation());
}

#undef PUSH
#undef POP
#undef __
