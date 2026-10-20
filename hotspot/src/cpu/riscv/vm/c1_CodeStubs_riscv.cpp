/*
 * Copyright (c) 1999, 2020, Oracle and/or its affiliates. All rights reserved.
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
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "nativeInst_riscv.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_riscv.inline.hpp"
#if INCLUDE_ALL_GCS
#include "gc_implementation/g1/g1SATBCardTableModRefBS.hpp"
#endif


#define __ ce->masm()->

float ConversionStub::float_zero = 0.0;
double ConversionStub::double_zero = 0.0;

static Register as_reg(LIR_Opr op) {
  return op->is_double_cpu() ? op->as_register_lo() : op->as_register();
}

void ConversionStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);

  __ enter();
  __ subi(sp, sp, 2 * wordSize);
  __ push_reg(RegSet::range(x5, x31), sp); // integer registers except x0..x4
  for (int i = 30; i >= 0; i -= 2) {
    // caller-saved fp registers (f0..f7, f10..f17, f28..f31)
    if (i < 8 || (i >= 10 && i <= 16) || i >= 28) {
      __ subi(sp, sp, 2 * wordSize);
      __ fsd(as_FloatRegister(i), Address(sp, 0));
      __ fsd(as_FloatRegister(i + 1), Address(sp, wordSize));
    }
  }

  switch (bytecode()) {
  case Bytecodes::_f2i:
    if (f10 != input()->as_float_reg()) {
      __ fmv_s(f10, input()->as_float_reg());
    }
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::f2i));
    break;
  case Bytecodes::_d2i:
    if (f10 != input()->as_double_reg()) {
      __ fmv_d(f10, input()->as_double_reg());
    }
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::d2i));
    break;
  case Bytecodes::_f2l:
    if (f10 != input()->as_float_reg()) {
      __ fmv_s(f10, input()->as_float_reg());
    }
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::f2l));
    break;
  case Bytecodes::_d2l:
    if (f10 != input()->as_double_reg()) {
      __ fmv_d(f10, input()->as_double_reg());
    }
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::d2l));
    break;
  default:
    ShouldNotReachHere();
  }

  // Note: enter() saves ra at [fp - 8] and old_fp at [fp - 16], so the
  // 2*wordSize scratch slot allocated after enter() is at [fp - 3*wordSize].
  __ sd(x10, Address(fp, -3 * wordSize));

  for (int i = 0; i < 32; i += 2) {
    if (i < 8 || (i >= 10 && i <= 16) || i >= 28) {
      __ fld(as_FloatRegister(i), Address(sp, 0));
      __ fld(as_FloatRegister(i + 1), Address(sp, wordSize));
      __ addi(sp, sp, 2 * wordSize);
    }
  }
  __ pop_reg(RegSet::range(x5, x31), sp);

  __ ld(as_reg(result()), Address(fp, -3 * wordSize));
  __ leave();

  __ j(_continuation);
}

void CounterOverflowStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  if (_method->is_cpu_register()) {
    ce->store_parameter(_method->as_register(), 1);
  } else {
    Metadata* m = _method->as_constant_ptr()->as_metadata();
    __ mov_metadata(t0, m);
    ce->store_parameter(t0, 1);
  }
  ce->store_parameter(_bci, 0);
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::counter_overflow_id)));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  __ j(_continuation);
}

RangeCheckStub::RangeCheckStub(CodeEmitInfo* info, LIR_Opr index,
                               bool throw_index_out_of_bounds_exception)
  : _throw_index_out_of_bounds_exception(throw_index_out_of_bounds_exception)
  , _index(index)
{
  assert(info != NULL, "must have info");
  _info = new CodeEmitInfo(info);
}

void RangeCheckStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  if (_info->deoptimize_on_exception()) {
    address a = Runtime1::entry_for(Runtime1::predicate_failed_trap_id);
    __ far_call(RuntimeAddress(a));
    ce->add_call_info_here(_info);
    ce->verify_oop_map(_info);
    debug_only(__ should_not_reach_here());
    return;
  }

  if (_index->is_cpu_register()) {
    __ mv(t0, _index->as_register());
  } else {
    __ mv(t0, _index->as_jint());
  }
  Runtime1::StubID stub_id;
  if (_throw_index_out_of_bounds_exception) {
    stub_id = Runtime1::throw_index_exception_id;
  } else {
    stub_id = Runtime1::throw_range_check_failed_id;
  }
  __ far_call(RuntimeAddress(Runtime1::entry_for(stub_id)), NULL, t1);
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  debug_only(__ should_not_reach_here());
}

PredicateFailedStub::PredicateFailedStub(CodeEmitInfo* info) {
  _info = new CodeEmitInfo(info);
}

void PredicateFailedStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  address a = Runtime1::entry_for(Runtime1::predicate_failed_trap_id);
  __ far_call(RuntimeAddress(a));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  debug_only(__ should_not_reach_here());
}

void DivByZeroStub::emit_code(LIR_Assembler* ce) {
  if (_offset != -1) {
    ce->compilation()->implicit_exception_table()->append(_offset, __ offset());
  }
  __ bind(_entry);
  __ far_call(Address(Runtime1::entry_for(Runtime1::throw_div0_exception_id), relocInfo::runtime_call_type));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
#ifdef ASSERT
  __ should_not_reach_here();
#endif
}

// Implementation of NewInstanceStub
NewInstanceStub::NewInstanceStub(LIR_Opr klass_reg, LIR_Opr result, ciInstanceKlass* klass, CodeEmitInfo* info, Runtime1::StubID stub_id) {
  _result = result;
  _klass = klass;
  _klass_reg = klass_reg;
  _info = new CodeEmitInfo(info);
  assert(stub_id == Runtime1::new_instance_id                 ||
         stub_id == Runtime1::fast_new_instance_id            ||
         stub_id == Runtime1::fast_new_instance_init_check_id,
         "need new_instance id");
  _stub_id = stub_id;
}

void NewInstanceStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  __ mv(x13, _klass_reg->as_register());
  __ far_call(RuntimeAddress(Runtime1::entry_for(_stub_id)));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == x10, "result must in x10");
  __ j(_continuation);
}

// Implementation of NewTypeArrayStub
NewTypeArrayStub::NewTypeArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _length = length;
  _result = result;
  _info = new CodeEmitInfo(info);
}

void NewTypeArrayStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  assert(_length->as_register() == x9, "length must in x9");
  assert(_klass_reg->as_register() == x13, "klass_reg must in x13");
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::new_type_array_id)));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == x10, "result must in x10");
  __ j(_continuation);
}

// Implementation of NewObjectArrayStub
NewObjectArrayStub::NewObjectArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _result = result;
  _length = length;
  _info = new CodeEmitInfo(info);
}

void NewObjectArrayStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  assert(_length->as_register() == x9, "length must in x9");
  assert(_klass_reg->as_register() == x13, "klass_reg must in x13");
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::new_object_array_id)));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == x10, "result must in x10");
  __ j(_continuation);
}

// Implementation of MonitorAccessStubs
MonitorEnterStub::MonitorEnterStub(LIR_Opr obj_reg, LIR_Opr lock_reg, CodeEmitInfo* info)
: MonitorAccessStub(obj_reg, lock_reg) {
  _info = new CodeEmitInfo(info);
}

void MonitorEnterStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  ce->store_parameter(_obj_reg->as_register(),  1);
  ce->store_parameter(_lock_reg->as_register(), 0);
  Runtime1::StubID enter_id;
  if (ce->compilation()->has_fpu_code()) {
    enter_id = Runtime1::monitorenter_id;
  } else {
    enter_id = Runtime1::monitorenter_nofpu_id;
  }
  __ far_call(RuntimeAddress(Runtime1::entry_for(enter_id)));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  __ j(_continuation);
}

void MonitorExitStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  if (_compute_lock) {
    // lock_reg was destroyed by fast unlocking attempt => recompute it
    ce->monitor_address(_monitor_ix, _lock_reg);
  }
  ce->store_parameter(_lock_reg->as_register(), 0);
  // note: non-blocking leaf routine => no call info needed
  Runtime1::StubID exit_id;
  if (ce->compilation()->has_fpu_code()) {
    exit_id = Runtime1::monitorexit_id;
  } else {
    exit_id = Runtime1::monitorexit_nofpu_id;
  }
  __ la(ra, _continuation);
  __ far_jump(RuntimeAddress(Runtime1::entry_for(exit_id)));
}

// Implementation of patching:
// Like AArch64, RISC-V does not patch C1-generated code in place;
// unresolved accesses trigger deoptimization via Runtime1::patch_code_riscv.

int PatchingStub::_patch_info_offset = -NativeGeneralJump::instruction_size;

void PatchingStub::align_patch_site(MacroAssembler* masm) {
}

void PatchingStub::emit_code(LIR_Assembler* ce) {
  assert(false, "RISC-V should not use C1 runtime patching");
}

void DeoptimizeStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::deoptimize_id)));
  ce->add_call_info_here(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

void ImplicitNullCheckStub::emit_code(LIR_Assembler* ce) {
  address a = NULL;
  if (_info->deoptimize_on_exception()) {
    // Deoptimize, do not throw the exception, because it is probably wrong to do it here.
    a = Runtime1::entry_for(Runtime1::predicate_failed_trap_id);
  } else {
    a = Runtime1::entry_for(Runtime1::throw_null_pointer_exception_id);
  }

  ce->compilation()->implicit_exception_table()->append(_offset, __ offset());
  __ bind(_entry);
  __ far_call(RuntimeAddress(a));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  debug_only(__ should_not_reach_here());
}

void SimpleExceptionStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");

  __ bind(_entry);
  // pass the object in a tmp register because all other registers
  // must be preserved
  if (_obj->is_cpu_register()) {
    __ mv(t0, _obj->as_register());
  }
  __ far_call(RuntimeAddress(Runtime1::entry_for(_stub)), NULL, t1);
  ce->add_call_info_here(_info);
  debug_only(__ should_not_reach_here());
}

void ArrayCopyStub::emit_code(LIR_Assembler* ce) {
  // ---------------slow case: call to native-----------------
  __ bind(_entry);
  // Figure out where the args should go
  const int args_num = 5;
  VMRegPair args[args_num];
  BasicType signature[args_num] = { T_OBJECT, T_INT, T_OBJECT, T_INT, T_INT };
  SharedRuntime::java_calling_convention(signature, args, args_num, true);

  // push parameters: (src, src_pos, dest, destPos, length)
  Register r[args_num];
  r[0] = src()->as_register();
  r[1] = src_pos()->as_register();
  r[2] = dst()->as_register();
  r[3] = dst_pos()->as_register();
  r[4] = length()->as_register();

  // next registers will get stored on the stack
  for (int j = 0; j < args_num; j++) {
    VMReg r_1 = args[j].first();
    if (r_1->is_stack()) {
      int st_off = r_1->reg2stack() * wordSize;
      __ sd(r[j], Address(sp, st_off));
    } else {
      assert(r[j] == args[j].first()->as_Register(), "Wrong register for arg");
    }
  }

  ce->align_call(lir_static_call);

  ce->emit_static_call_stub();
  if (ce->compilation()->bailed_out()) {
    return; // CodeCache is full
  }
  Address resolve(SharedRuntime::get_resolve_static_call_stub(),
                  relocInfo::static_call_type);
  address call = __ trampoline_call(resolve);
  if (call == NULL) {
    ce->bailout("trampoline stub overflow");
    return;
  }
  ce->add_call_info_here(info());

#ifndef PRODUCT
  __ la(t1, ExternalAddress((address)&Runtime1::_arraycopy_slowcase_cnt));
  __ add_memory_int32(Address(t1, 0), 1);
#endif

  __ j(_continuation);
}

/////////////////////////////////////////////////////////////////////////////
#if INCLUDE_ALL_GCS

void G1PreBarrierStub::emit_code(LIR_Assembler* ce) {
  // At this point we know that marking is in progress.
  // If do_load() is true then we have to emit the
  // load of the previous value; otherwise it has already
  // been loaded into _pre_val.

  __ bind(_entry);
  assert(pre_val()->is_register(), "Precondition.");

  Register pre_val_reg = pre_val()->as_register();

  if (do_load()) {
    ce->mem2reg(addr(), pre_val(), T_OBJECT, patch_code(), info(), false /*wide*/, false /*unaligned*/);
  }
  __ beqz(pre_val_reg, _continuation, /* is_far */ true);
  ce->store_parameter(pre_val()->as_register(), 0);
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::g1_pre_barrier_slow_id)));
  __ j(_continuation);
}

jbyte* G1PostBarrierStub::_byte_map_base = NULL;

jbyte* G1PostBarrierStub::byte_map_base_slow() {
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->is_a(BarrierSet::G1SATBCTLogging),
         "Must be if we're using this.");
  return ((G1SATBCardTableModRefBS*)bs)->byte_map_base;
}

void G1PostBarrierStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  assert(addr()->is_register(), "Precondition.");
  assert(new_val()->is_register(), "Precondition.");
  Register new_val_reg = new_val()->as_register();
  __ beqz(new_val_reg, _continuation, /* is_far */ true);
  ce->store_parameter(addr()->as_pointer_register(), 0);
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::g1_post_barrier_slow_id)));
  __ j(_continuation);
}

#endif // INCLUDE_ALL_GCS
/////////////////////////////////////////////////////////////////////////////

#undef __
