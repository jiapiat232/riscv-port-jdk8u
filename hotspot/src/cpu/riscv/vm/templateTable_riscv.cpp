/*
 * Copyright (c) 2003, 2017, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
 * Copyright (c) 2020, 2022, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "memory/universe.inline.hpp"
#include "oops/methodData.hpp"
#include "oops/method.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"

#ifndef CC_INTERP

#define __ _masm->

// Address computation: local variables

static inline Address iaddress(int n) {
  return Address(xlocals, Interpreter::local_offset_in_bytes(n));
}

static inline Address laddress(int n) {
  return iaddress(n + 1);
}

static inline Address faddress(int n) {
  return iaddress(n);
}

static inline Address daddress(int n) {
  return laddress(n);
}

static inline Address aaddress(int n) {
  return iaddress(n);
}

static inline Address iaddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  _masm->shift_left_add(temp, r, xlocals, 3);
  return Address(temp, 0);
}

static inline Address laddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  _masm->shift_left_add(temp, r, xlocals, 3);
  return Address(temp, Interpreter::local_offset_in_bytes(1));
}

static inline Address faddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return iaddress(r, temp, _masm);
}

static inline Address daddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return laddress(r, temp, _masm);
}

static inline Address aaddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return iaddress(r, temp, _masm);
}

static inline Address at_rsp() {
  return Address(esp, 0);
}

static inline Address at_tos() {
  return Address(esp, Interpreter::expr_offset_in_bytes(0));
}

static inline Address at_tos_p1() {
  return Address(esp, Interpreter::expr_offset_in_bytes(1));
}

static inline Address at_tos_p2() {
  return Address(esp, Interpreter::expr_offset_in_bytes(2));
}

static inline Address at_tos_p3() {
  return Address(esp, Interpreter::expr_offset_in_bytes(3));
}

static inline Address at_tos_p4() {
  return Address(esp, Interpreter::expr_offset_in_bytes(4));
}

static inline Address at_tos_p5() {
  return Address(esp, Interpreter::expr_offset_in_bytes(5));
}

void TemplateTable::pd_initialize() {
  // No riscv-specific initialization needed
}

Address TemplateTable::at_bcp(int offset) {
  assert(_desc->uses_bcp(), "inconsistent uses_bcp information");
  return Address(xbcp, offset);
}

// Miscellaneous helper routines
// Store an oop (or NULL) at the Address described by obj.
// If val == noreg this means store a NULL
static void do_oop_store(InterpreterMacroAssembler* _masm,
                         Address obj,
                         Register val,
                         BarrierSet::Name barrier,
                         bool precise) {
  assert(val == noreg || val == x10, "parameter is just for looks");
  switch (barrier) {
#if INCLUDE_ALL_GCS
    case BarrierSet::G1SATBCT:
    case BarrierSet::G1SATBCTLogging:
      {
        // flatten object address if needed
        if (obj.offset() == 0) {
          if (obj.base() != x13) {
            __ mv(x13, obj.base());
          }
        } else {
          __ la(x13, obj);
        }
        __ g1_write_barrier_pre(x13 /* obj */,
                                x11 /* pre_val */,
                                xthread /* thread */,
                                t1  /* tmp */,
                                val != noreg /* tosca_live */,
                                false /* expand_call */);
        if (val == noreg) {
          __ store_heap_oop_null(Address(x13, 0));
        } else {
          // G1 barrier needs uncompressed oop for region cross check.
          Register new_val = val;
          if (UseCompressedOops) {
            new_val = t1;
            __ mv(new_val, val);
          }
          __ store_heap_oop(Address(x13, 0), val);
          __ g1_write_barrier_post(x13 /* store_adr */,
                                   new_val /* new_val */,
                                   xthread /* thread */,
                                   t2 /* tmp */,
                                   x11 /* tmp2 */);
        }
      }
      break;
#endif // INCLUDE_ALL_GCS
    case BarrierSet::CardTableModRef:
    case BarrierSet::CardTableExtension:
      {
        if (val == noreg) {
          __ store_heap_oop_null(obj);
        } else {
          __ store_heap_oop(obj, val);
          // flatten object address if needed
          if (!precise || obj.offset() == 0) {
            __ store_check(obj.base());
          } else {
            __ la(t0, obj);
            __ store_check(t0);
          }
        }
      }
      break;
    case BarrierSet::ModRef:
    case BarrierSet::Other:
      if (val == noreg) {
        __ store_heap_oop_null(obj);
      } else {
        __ store_heap_oop(obj, val);
      }
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::patch_bytecode(Bytecodes::Code bc, Register bc_reg,
                                   Register temp_reg, bool load_bc_into_bc_reg /*=true*/,
                                   int byte_no) {
  assert_different_registers(bc_reg, temp_reg);
  if (!RewriteBytecodes) return;
  Label L_patch_done;

  switch (bc) {
    case Bytecodes::_fast_aputfield:
    case Bytecodes::_fast_bputfield:
    case Bytecodes::_fast_zputfield:
    case Bytecodes::_fast_cputfield:
    case Bytecodes::_fast_dputfield:
    case Bytecodes::_fast_fputfield:
    case Bytecodes::_fast_iputfield:
    case Bytecodes::_fast_lputfield:
    case Bytecodes::_fast_sputfield: {
      assert(byte_no == f1_byte || byte_no == f2_byte, "byte_no out of range");
      assert(load_bc_into_bc_reg, "we use bc_reg as temp");
      __ get_cache_and_index_and_bytecode_at_bcp(temp_reg, bc_reg, temp_reg, byte_no, 1);
      __ mv(bc_reg, bc);
      __ beqz(temp_reg, L_patch_done); // don't patch
      break;
    }
    default:
      assert(byte_no == -1, "sanity");
      if (load_bc_into_bc_reg) {
        __ mv(bc_reg, bc);
      }
  }

  if (JvmtiExport::can_post_breakpoint()) {
    Label L_fast_patch;
    __ lbu(temp_reg, at_bcp(0));
    __ subi(temp_reg, temp_reg, Bytecodes::_breakpoint);
    __ bnez(temp_reg, L_fast_patch);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::set_original_bytecode_at), xmethod, xbcp, bc_reg);
    __ j(L_patch_done);
    __ bind(L_fast_patch);
  }

#ifdef ASSERT
  Label L_okay;
  __ lbu(temp_reg, at_bcp(0));
  __ beq(temp_reg, bc_reg, L_okay);
  __ subi(temp_reg, temp_reg, (int)Bytecodes::java_code(bc));
  __ beqz(temp_reg, L_okay);
  __ stop("patching the wrong bytecode");
  __ bind(L_okay);
#endif

  // patch bytecode with release ordering
  __ membar(MacroAssembler::StoreStore);
  __ sb(bc_reg, at_bcp(0));
  __ bind(L_patch_done);
}

// -------------------------------------------------------------
// Constants
// -------------------------------------------------------------

void TemplateTable::nop() {
  transition(vtos, vtos);
}

void TemplateTable::shouldnotreachhere() {
  transition(vtos, vtos);
  __ stop("should not reach here bytecode");
}

void TemplateTable::aconst_null() {
  transition(vtos, atos);
  __ mv(x10, zr);
}

void TemplateTable::iconst(int value) {
  transition(vtos, itos);
  __ mv(x10, value);
}

void TemplateTable::lconst(int value) {
  transition(vtos, ltos);
  __ mv(x10, value);
}

void TemplateTable::fconst(int value) {
  transition(vtos, ftos);
  static float fBuf[2] = {1.0f, 2.0f};
  __ mv(t0, (intptr_t)fBuf);
  switch (value) {
    case 0:
      __ fmv_w_x(f10, zr);
      break;
    case 1:
      __ flw(f10, Address(t0, 0));
      break;
    case 2:
      __ flw(f10, Address(t0, sizeof(float)));
      break;
    default:
      ShouldNotReachHere();
      break;
  }
}

void TemplateTable::dconst(int value) {
  transition(vtos, dtos);
  static double dBuf[1] = {1.0};
  __ mv(t0, (intptr_t)dBuf);
  switch (value) {
    case 0:
      __ fmv_d_x(f10, zr);
      break;
    case 1:
      __ fld(f10, Address(t0, 0));
      break;
    default:
      ShouldNotReachHere();
      break;
  }
}

void TemplateTable::bipush() {
  transition(vtos, itos);
  __ lb(x10, at_bcp(1));
}

void TemplateTable::sipush() {
  transition(vtos, itos);
  __ lb(x10, at_bcp(1));
  __ lbu(t1, at_bcp(2));
  __ slli(x10, x10, 8);
  __ orr(x10, x10, t1);
}

void TemplateTable::ldc(bool wide) {
  transition(vtos, vtos);
  Label call_ldc, notFloat, notClass, Done;

  if (wide) {
    __ get_unsigned_2_byte_index_at_bcp(x11, 1);
  } else {
    __ lbu(x11, at_bcp(1));
  }
  __ get_cpool_and_tags(x12, x10);

  const int base_offset = ConstantPool::header_size() * wordSize;
  const int tags_offset = Array<u1>::base_offset_in_bytes();

  // get type
  __ addi(t0, x11, tags_offset);
  __ add(t0, x10, t0);
  __ lbu(t0, Address(t0, 0));

  // unresolved class - get the resolved class
  __ subi(t1, t0, JVM_CONSTANT_UnresolvedClass);
  __ beqz(t1, call_ldc);

  // unresolved class in error state
  __ subi(t1, t0, JVM_CONSTANT_UnresolvedClassInError);
  __ beqz(t1, call_ldc);

  // resolved class - need to call vm to get java mirror of the class
  __ subi(t1, t0, JVM_CONSTANT_Class);
  __ bnez(t1, notClass);

  __ bind(call_ldc);
  __ mv(c_rarg1, wide);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::ldc), c_rarg1);
  __ push_ptr(x10);
  __ verify_oop(x10);
  __ j(Done);

  __ bind(notClass);
  __ subi(t1, t0, JVM_CONSTANT_Float);
  __ bnez(t1, notFloat);
  // ftos
  __ slli(x11, x11, LogBytesPerWord);
  __ add(x11, x12, x11);
  __ flw(f10, Address(x11, base_offset));
  __ push_f();
  __ j(Done);

  __ bind(notFloat);
#ifdef ASSERT
  {
    Label L;
    __ subi(t1, t0, JVM_CONSTANT_Integer);
    __ beqz(t1, L);
    __ stop("unexpected tag type in ldc");
    __ bind(L);
  }
#endif
  // itos JVM_CONSTANT_Integer only
  __ slli(x11, x11, LogBytesPerWord);
  __ add(x11, x12, x11);
  __ lw(x10, Address(x11, base_offset));
  __ push_i(x10);
  __ bind(Done);
}

void TemplateTable::fast_aldc(bool wide) {
  transition(vtos, atos);

  Register result = x10;
  Register tmp = x11;
  int index_size = wide ? sizeof(u2) : sizeof(u1);
  Label resolved;

  assert_different_registers(result, tmp);
  __ get_cache_index_at_bcp(tmp, 1, index_size);
  __ load_resolved_reference_at_index(result, tmp);
  __ bnez(result, resolved);

  address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_ldc);
  __ mv(tmp, (int)bytecode());
  __ call_VM(result, entry, tmp);

  __ bind(resolved);
  if (VerifyOops) {
    __ verify_oop(result);
  }
}

void TemplateTable::ldc2_w() {
  transition(vtos, vtos);
  Label Long, Done;
  __ get_unsigned_2_byte_index_at_bcp(x10, 1);
  __ get_cpool_and_tags(x11, x12);

  const int base_offset = ConstantPool::header_size() * wordSize;
  const int tags_offset = Array<u1>::base_offset_in_bytes();

  __ add(t0, x12, x10);
  __ lbu(t0, Address(t0, tags_offset));
  __ subi(t1, t0, (int)JVM_CONSTANT_Double);
  __ bnez(t1, Long);

  // dtos
  __ slli(t2, x10, LogBytesPerWord);
  __ add(t2, x11, t2);
  __ fld(f10, Address(t2, base_offset));
  __ push_d();
  __ j(Done);

  __ bind(Long);
  // ltos
  __ slli(t2, x10, LogBytesPerWord);
  __ add(t2, x11, t2);
  __ ld(x10, Address(t2, base_offset));
  __ push_l();

  __ bind(Done);
}

// -------------------------------------------------------------
// Locals Loading
// -------------------------------------------------------------

void TemplateTable::locals_index(Register reg, int offset) {
  __ lbu(reg, at_bcp(offset));
  __ neg(reg, reg);
}

void TemplateTable::locals_index_wide(Register reg) {
  assert_different_registers(reg, t1);
  __ lbu(reg, at_bcp(2));
  __ lbu(t1, at_bcp(3));
  __ slli(reg, reg, 8);
  __ orr(reg, reg, t1);
  __ neg(reg, reg);
}

void TemplateTable::iload() {
  transition(vtos, itos);
  if (RewriteFrequentPairs) {
    Label rewrite, done;
    const Register bc = x14;

    __ lbu(x11, at_bcp(Bytecodes::length_for(Bytecodes::_iload)));
    __ subi(t0, x11, Bytecodes::_iload);
    __ beqz(t0, done);

    __ subi(t0, x11, Bytecodes::_fast_iload);
    __ mv(bc, Bytecodes::_fast_iload2);
    __ beqz(t0, rewrite);

    __ subi(t0, x11, Bytecodes::_caload);
    __ mv(bc, Bytecodes::_fast_icaload);
    __ beqz(t0, rewrite);

    __ mv(bc, Bytecodes::_fast_iload);

    __ bind(rewrite);
    patch_bytecode(Bytecodes::_iload, bc, x11, false);
    __ bind(done);
  }

  locals_index(x11);
  __ lw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::fast_iload2() {
  transition(vtos, itos);
  locals_index(x11);
  __ lw(x10, iaddress(x11, t0, _masm));
  __ push(itos);
  locals_index(x11, 3);
  __ lw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::fast_iload() {
  transition(vtos, itos);
  locals_index(x11);
  __ lw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::fast_icaload() {
  transition(vtos, itos);
  locals_index(x12);
  __ ld(x11, iaddress(x12, t0, _masm));
  __ pop_ptr(x10);

  index_check(x10, x11);
  __ slli(t0, x11, 1);
  __ add(t0, x10, t0);
  __ lhu(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_CHAR)));
}

void TemplateTable::lload() {
  transition(vtos, ltos);
  locals_index(x11);
  __ ld(x10, laddress(x11, t0, _masm));
}

void TemplateTable::fload() {
  transition(vtos, ftos);
  locals_index(x11);
  __ flw(f10, faddress(x11, t0, _masm));
}

void TemplateTable::dload() {
  transition(vtos, dtos);
  locals_index(x11);
  __ fld(f10, daddress(x11, t0, _masm));
}

void TemplateTable::aload() {
  transition(vtos, atos);
  locals_index(x11);
  __ ld(x10, aaddress(x11, t0, _masm));
}

void TemplateTable::iload(int n) {
  transition(vtos, itos);
  __ lw(x10, iaddress(n));
}

void TemplateTable::lload(int n) {
  transition(vtos, ltos);
  __ ld(x10, laddress(n));
}

void TemplateTable::fload(int n) {
  transition(vtos, ftos);
  __ flw(f10, faddress(n));
}

void TemplateTable::dload(int n) {
  transition(vtos, dtos);
  __ fld(f10, daddress(n));
}

void TemplateTable::aload(int n) {
  transition(vtos, atos);
  __ ld(x10, aaddress(n));
}

void TemplateTable::aload_0() {
  aload(0);
}

void TemplateTable::wide_iload() {
  transition(vtos, itos);
  locals_index_wide(x11);
  __ lw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::wide_lload() {
  transition(vtos, ltos);
  locals_index_wide(x11);
  __ ld(x10, laddress(x11, t0, _masm));
}

void TemplateTable::wide_fload() {
  transition(vtos, ftos);
  locals_index_wide(x11);
  __ flw(f10, faddress(x11, t0, _masm));
}

void TemplateTable::wide_dload() {
  transition(vtos, dtos);
  locals_index_wide(x11);
  __ fld(f10, daddress(x11, t0, _masm));
}

void TemplateTable::wide_aload() {
  transition(vtos, atos);
  locals_index_wide(x11);
  __ ld(x10, aaddress(x11, t0, _masm));
}


// -------------------------------------------------------------
// Locals Storing
// -------------------------------------------------------------

void TemplateTable::istore() {
  transition(itos, vtos);
  locals_index(x11);
  __ sw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::lstore() {
  transition(ltos, vtos);
  locals_index(x11);
  __ sd(x10, laddress(x11, t0, _masm));
}

void TemplateTable::fstore() {
  transition(ftos, vtos);
  locals_index(x11);
  __ fsw(f10, faddress(x11, t0, _masm));
}

void TemplateTable::dstore() {
  transition(dtos, vtos);
  locals_index(x11);
  __ fsd(f10, daddress(x11, t0, _masm));
}

void TemplateTable::astore() {
  transition(vtos, vtos);
  __ pop_ptr(x10);
  locals_index(x11);
  __ sd(x10, aaddress(x11, t0, _masm));
}

void TemplateTable::istore(int n) {
  transition(itos, vtos);
  __ sw(x10, iaddress(n));
}

void TemplateTable::lstore(int n) {
  transition(ltos, vtos);
  __ sd(x10, laddress(n));
}

void TemplateTable::fstore(int n) {
  transition(ftos, vtos);
  __ fsw(f10, faddress(n));
}

void TemplateTable::dstore(int n) {
  transition(dtos, vtos);
  __ fsd(f10, daddress(n));
}

void TemplateTable::astore(int n) {
  transition(vtos, vtos);
  __ pop_ptr(x10);
  __ sd(x10, aaddress(n));
}

void TemplateTable::wide_istore() {
  transition(vtos, vtos);
  __ pop_i();
  locals_index_wide(x11);
  __ sw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::wide_lstore() {
  transition(vtos, vtos);
  __ pop_l();
  locals_index_wide(x11);
  __ sd(x10, laddress(x11, t0, _masm));
}

void TemplateTable::wide_fstore() {
  transition(vtos, vtos);
  __ pop_f();
  locals_index_wide(x11);
  __ fsw(f10, faddress(x11, t0, _masm));
}

void TemplateTable::wide_dstore() {
  transition(vtos, vtos);
  __ pop_d();
  locals_index_wide(x11);
  __ fsd(f10, daddress(x11, t0, _masm));
}

void TemplateTable::wide_astore() {
  transition(vtos, vtos);
  __ pop_ptr(x10);
  locals_index_wide(x11);
  __ sd(x10, aaddress(x11, t0, _masm));
}

// -------------------------------------------------------------
// Array Checks, Loading and Storing
// -------------------------------------------------------------

void TemplateTable::index_check(Register array, Register index) {
  // destroys x11, t0, t1
  const Register length = t0;
  __ lwu(length, Address(array, arrayOopDesc::length_offset_in_bytes()));
  if (index != x11) {
    assert(x11 != array, "different registers");
    __ mv(x11, index);
  }
  Label ok;
  __ bltu(index, length, ok);
  __ mv(x13, array);
  __ mv(t1, Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ jr(t1);
  __ bind(ok);
}

void TemplateTable::index_check_without_pop(Register array, Register index) {
  index_check(array, index);
}

void TemplateTable::iaload() {
  transition(itos, itos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, 2);
  __ add(t0, x10, t0);
  __ lw(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_INT)));
}

void TemplateTable::laload() {
  transition(itos, ltos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, LogBytesPerWord);
  __ add(t0, x10, t0);
  __ ld(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_LONG)));
}

void TemplateTable::faload() {
  transition(itos, ftos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, 2);
  __ add(t0, x10, t0);
  __ flw(f10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_FLOAT)));
}

void TemplateTable::daload() {
  transition(itos, dtos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, LogBytesPerWord);
  __ add(t0, x10, t0);
  __ fld(f10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_DOUBLE)));
}

void TemplateTable::aaload() {
  transition(itos, atos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, LogBytesPerHeapOop);
  __ add(t0, x10, t0);
  __ load_heap_oop(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_OBJECT)));
  __ verify_oop(x10);
}

void TemplateTable::baload() {
  transition(itos, itos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ add(t0, x10, x11);
  __ lb(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_BYTE)));
}

void TemplateTable::caload() {
  transition(itos, itos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, 1);
  __ add(t0, x10, t0);
  __ lhu(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_CHAR)));
}

void TemplateTable::saload() {
  transition(itos, itos);
  __ mv(x11, x10);
  __ pop_ptr(x10);
  index_check(x10, x11);
  __ slli(t0, x11, 1);
  __ add(t0, x10, t0);
  __ lh(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_SHORT)));
}

void TemplateTable::iastore() {
  transition(itos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  __ slli(t0, x11, 2);
  __ add(t0, x13, t0);
  __ sw(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_INT)));
}

void TemplateTable::lastore() {
  transition(ltos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  __ slli(t0, x11, LogBytesPerWord);
  __ add(t0, x13, t0);
  __ sd(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_LONG)));
}

void TemplateTable::fastore() {
  transition(ftos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  __ slli(t0, x11, 2);
  __ add(t0, x13, t0);
  __ fsw(f10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_FLOAT)));
}

void TemplateTable::dastore() {
  transition(dtos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  __ slli(t0, x11, LogBytesPerWord);
  __ add(t0, x13, t0);
  __ fsd(f10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_DOUBLE)));
}

void TemplateTable::aastore() {
  Label is_null, ok_is_subtype, done;
  transition(vtos, vtos);
  // stack: ..., array, index, value
  __ ld(x10, at_tos());    // value
  __ lw(x12, at_tos_p1()); // index
  __ ld(x13, at_tos_p2()); // array

  index_check(x13, x12); // kills x11

  // Address of element
  __ addi(x14, x12, arrayOopDesc::base_offset_in_bytes(T_OBJECT) >> LogBytesPerHeapOop);
  __ shift_left_add(x14, x14, x13, LogBytesPerHeapOop);
  Address element_address(x14, 0);

  // do array store check - check for NULL value first
  __ beqz(x10, is_null);

  // Move subklass into x11
  __ load_klass(x11, x10);
  // Move superklass into x10
  __ load_klass(x10, x13);
  __ ld(x10, Address(x10, ObjArrayKlass::element_klass_offset()));

  // Generate subtype check
  __ gen_subtype_check(x11, ok_is_subtype);

  // Come here on failure
  __ j(RuntimeAddress(Interpreter::_throw_ArrayStoreException_entry));

  // Come here on success
  __ bind(ok_is_subtype);

  // Get the value we will store
  __ ld(x10, at_tos());
  do_oop_store(_masm, element_address, x10, _bs->kind(), true);
  __ j(done);

  // Have a NULL in x10
  __ bind(is_null);
  __ profile_null_seen(x12);

  do_oop_store(_masm, element_address, noreg, _bs->kind(), true);

  // Pop stack arguments
  __ bind(done);
  __ addi(esp, esp, 3 * Interpreter::stackElementSize);
}

void TemplateTable::bastore() {
  transition(itos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  Label is_bool, done;
  __ load_klass(x12, x13);
  __ lwu(x12, Address(x12, ArrayKlass::layout_helper_offset()));
  int bt_shift = Klass::_lh_element_type_shift;
  __ srli(x12, x12, bt_shift);
  __ andi(x12, x12, 0xff);
  __ subi(t0, x12, T_BOOLEAN);
  __ beqz(t0, is_bool);

  __ add(t0, x13, x11);
  __ sb(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_BYTE)));
  __ j(done);

  __ bind(is_bool);
  __ andi(x10, x10, 1);
  __ add(t0, x13, x11);
  __ sb(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_BOOLEAN)));

  __ bind(done);
}

void TemplateTable::castore() {
  transition(itos, vtos);
  __ pop_i(x11);
  __ pop_ptr(x13);
  index_check(x13, x11);
  __ slli(t0, x11, 1);
  __ add(t0, x13, t0);
  __ sh(x10, Address(t0, arrayOopDesc::base_offset_in_bytes(T_CHAR)));
}

void TemplateTable::sastore() {
  castore();
}

// -------------------------------------------------------------
// Stack Operations (pop, dup, swap)
// -------------------------------------------------------------

void TemplateTable::pop() {
  transition(vtos, vtos);
  __ addi(esp, esp, Interpreter::stackElementSize);
}

void TemplateTable::pop2() {
  transition(vtos, vtos);
  __ addi(esp, esp, 2 * Interpreter::stackElementSize);
}

void TemplateTable::dup() {
  transition(vtos, vtos);
  __ ld(x10, Address(esp, 0));
  __ push_ptr(x10);
}

void TemplateTable::dup_x1() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld(x10, at_tos());    // load b
  __ ld(x12, at_tos_p1()); // load a
  __ sd(x10, at_tos_p1()); // store b
  __ sd(x12, at_tos());    // store a
  __ push_ptr(x10);        // push b
}

void TemplateTable::dup_x2() {
  transition(vtos, vtos);
  // stack: ..., a, b, c
  __ ld(x10, at_tos());    // load c
  __ ld(x12, at_tos_p2()); // load a
  __ sd(x10, at_tos_p2()); // store c in a
  __ push_ptr(x10);        // push c
  // stack: ..., c, b, c, c
  __ ld(x10, at_tos_p2()); // load b
  __ sd(x12, at_tos_p2()); // store a in b
  // stack: ..., c, a, c, c
  __ sd(x10, at_tos_p1()); // store b in c
}

void TemplateTable::dup2() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld(x10, at_tos_p1()); // load a
  __ push_ptr(x10);        // push a
  __ ld(x10, at_tos_p1()); // load b
  __ push_ptr(x10);        // push b
}

void TemplateTable::dup2_x1() {
  transition(vtos, vtos);
  // stack: ..., a, b, c
  __ ld(x12, at_tos());    // load c
  __ ld(x10, at_tos_p1()); // load b
  __ push_ptr(x10);        // push b
  __ push_ptr(x12);        // push c
  // stack: ..., a, b, c, b, c
  __ sd(x12, at_tos_p3()); // store c in b
  // stack: ..., a, c, c, b, c
  __ ld(x12, at_tos_p4()); // load a
  __ sd(x12, at_tos_p2()); // store a in 2nd c
  // stack: ..., a, c, a, b, c
  __ sd(x10, at_tos_p4()); // store b in a
}

void TemplateTable::dup2_x2() {
  transition(vtos, vtos);
  // stack: ..., a, b, c, d
  __ ld(x12, at_tos());    // load d
  __ ld(x10, at_tos_p1()); // load c
  __ push_ptr(x10);        // push c
  __ push_ptr(x12);        // push d
  // stack: ..., a, b, c, d, c, d
  __ ld(x10, at_tos_p4()); // load b
  __ sd(x10, at_tos_p2()); // store b in d
  __ sd(x12, at_tos_p4()); // store d in b
  // stack: ..., a, d, c, b, c, d
  __ ld(x12, at_tos_p5()); // load a
  __ ld(x10, at_tos_p3()); // load c
  __ sd(x12, at_tos_p3()); // store a in c
  __ sd(x10, at_tos_p5()); // store c in a
}

void TemplateTable::swap() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld(x12, at_tos_p1()); // load a
  __ ld(x10, at_tos());    // load b
  __ sd(x12, at_tos());    // store a in b
  __ sd(x10, at_tos_p1()); // store b in a
}


// -------------------------------------------------------------
// Arithmetic and Logic Operations
// -------------------------------------------------------------

void TemplateTable::iop2(Operation op) {
  transition(itos, itos);
  __ pop_i(x11);
  switch (op) {
    case add  : __ addw(x10, x11, x10);  break;
    case sub  : __ subw(x10, x11, x10);  break;
    case mul  : __ mulw(x10, x11, x10);  break;
    case _and : __ andrw(x10, x11, x10); break;
    case _or  : __ orrw(x10, x11, x10);  break;
    case _xor : __ xorrw(x10, x11, x10); break;
    case shl  : __ sllw(x10, x11, x10);  break;
    case shr  : __ sraw(x10, x11, x10);  break;
    case ushr : __ srlw(x10, x11, x10);  break;
    default   : ShouldNotReachHere();
  }
}

void TemplateTable::lop2(Operation op) {
  transition(ltos, ltos);
  __ pop_l(x11);
  switch (op) {
    case add  : __ add(x10, x11, x10);  break;
    case sub  : __ sub(x10, x11, x10);  break;
    case mul  : __ mul(x10, x11, x10);  break;
    case _and : __ andr(x10, x11, x10); break;
    case _or  : __ orr(x10, x11, x10);  break;
    case _xor : __ xorr(x10, x11, x10); break;
    default   : ShouldNotReachHere();
  }
}

void TemplateTable::fop2(Operation op) {
  transition(ftos, ftos);
  switch (op) {
    case add:
      __ pop_f(f11);
      __ fadd_s(f10, f11, f10);
      break;
    case sub:
      __ pop_f(f11);
      __ fsub_s(f10, f11, f10);
      break;
    case mul:
      __ pop_f(f11);
      __ fmul_s(f10, f11, f10);
      break;
    case div:
      __ pop_f(f11);
      __ fdiv_s(f10, f11, f10);
      break;
    case rem:
      __ fmv_s(f11, f10);
      __ pop_f(f10);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::frem));
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::dop2(Operation op) {
  transition(dtos, dtos);
  switch (op) {
    case add:
      __ pop_d(f11);
      __ fadd_d(f10, f11, f10);
      break;
    case sub:
      __ pop_d(f11);
      __ fsub_d(f10, f11, f10);
      break;
    case mul:
      __ pop_d(f11);
      __ fmul_d(f10, f11, f10);
      break;
    case div:
      __ pop_d(f11);
      __ fdiv_d(f10, f11, f10);
      break;
    case rem:
      __ fmv_d(f11, f10);
      __ pop_d(f10);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::drem));
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::idiv() {
  transition(itos, itos);
  __ pop_i(x11);
  Label ok;
  __ bnez(x10, ok);
  __ j(RuntimeAddress(Interpreter::_throw_ArithmeticException_entry));
  __ bind(ok);
  __ divw(x10, x11, x10);
}

void TemplateTable::irem() {
  transition(itos, itos);
  __ pop_i(x11);
  Label ok;
  __ bnez(x10, ok);
  __ j(RuntimeAddress(Interpreter::_throw_ArithmeticException_entry));
  __ bind(ok);
  __ remw(x10, x11, x10);
}

void TemplateTable::lmul() {
  transition(ltos, ltos);
  __ pop_l(x11);
  __ mul(x10, x11, x10);
}

void TemplateTable::ldiv() {
  transition(ltos, ltos);
  __ pop_l(x11);
  Label ok;
  __ bnez(x10, ok);
  __ j(RuntimeAddress(Interpreter::_throw_ArithmeticException_entry));
  __ bind(ok);
  __ div(x10, x11, x10);
}

void TemplateTable::lrem() {
  transition(ltos, ltos);
  __ pop_l(x11);
  Label ok;
  __ bnez(x10, ok);
  __ j(RuntimeAddress(Interpreter::_throw_ArithmeticException_entry));
  __ bind(ok);
  __ rem(x10, x11, x10);
}

void TemplateTable::lshl() {
  transition(itos, ltos);
  __ pop_l(x11);
  __ sll(x10, x11, x10);
}

void TemplateTable::lshr() {
  transition(itos, ltos);
  __ pop_l(x11);
  __ sra(x10, x11, x10);
}

void TemplateTable::lushr() {
  transition(itos, ltos);
  __ pop_l(x11);
  __ srl(x10, x11, x10);
}

void TemplateTable::ineg() {
  transition(itos, itos);
  __ negw(x10, x10);
}

void TemplateTable::lneg() {
  transition(ltos, ltos);
  __ neg(x10, x10);
}

void TemplateTable::fneg() {
  transition(ftos, ftos);
  __ fneg_s(f10, f10);
}

void TemplateTable::dneg() {
  transition(dtos, dtos);
  __ fneg_d(f10, f10);
}

void TemplateTable::iinc() {
  transition(vtos, vtos);
  locals_index(x11);
  __ lb(t1, at_bcp(2));
  __ lw(x10, iaddress(x11, t0, _masm));
  __ addw(x10, x10, t1);
  __ sw(x10, iaddress(x11, t0, _masm));
}

void TemplateTable::wide_iinc() {
  transition(vtos, vtos);
  locals_index_wide(x11);
  __ lb(t1, at_bcp(4));
  __ lbu(t2, at_bcp(5));
  __ slli(t1, t1, 8);
  __ orr(t1, t1, t2);
  __ lw(x10, iaddress(x11, t0, _masm));
  __ addw(x10, x10, t1);
  __ sw(x10, iaddress(x11, t0, _masm));
}

// -------------------------------------------------------------
// Conversions
// -------------------------------------------------------------

void TemplateTable::convert() {
#ifdef ASSERT
  {
    TosState tos_in  = ilgl;
    TosState tos_out = ilgl;
    switch (bytecode()) {
      case Bytecodes::_i2l: // fall through
      case Bytecodes::_i2f: // fall through
      case Bytecodes::_i2d: // fall through
      case Bytecodes::_i2b: // fall through
      case Bytecodes::_i2c: // fall through
      case Bytecodes::_i2s: tos_in = itos; break;
      case Bytecodes::_l2i: // fall through
      case Bytecodes::_l2f: // fall through
      case Bytecodes::_l2d: tos_in = ltos; break;
      case Bytecodes::_f2i: // fall through
      case Bytecodes::_f2l: // fall through
      case Bytecodes::_f2d: tos_in = ftos; break;
      case Bytecodes::_d2i: // fall through
      case Bytecodes::_d2l: // fall through
      case Bytecodes::_d2f: tos_in = dtos; break;
      default             : ShouldNotReachHere();
    }
    switch (bytecode()) {
      case Bytecodes::_l2i: // fall through
      case Bytecodes::_f2i: // fall through
      case Bytecodes::_d2i: // fall through
      case Bytecodes::_i2b: // fall through
      case Bytecodes::_i2c: // fall through
      case Bytecodes::_i2s: tos_out = itos; break;
      case Bytecodes::_i2l: // fall through
      case Bytecodes::_f2l: // fall through
      case Bytecodes::_d2l: tos_out = ltos; break;
      case Bytecodes::_i2f: // fall through
      case Bytecodes::_l2f: // fall through
      case Bytecodes::_d2f: tos_out = ftos; break;
      case Bytecodes::_i2d: // fall through
      case Bytecodes::_l2d: // fall through
      case Bytecodes::_f2d: tos_out = dtos; break;
      default             : ShouldNotReachHere();
    }
    transition(tos_in, tos_out);
  }
#endif // ASSERT

  switch (bytecode()) {
    case Bytecodes::_i2l:
      __ sext(x10, x10, 32);
      break;
    case Bytecodes::_i2f:
      __ fcvt_s_w(f10, x10);
      break;
    case Bytecodes::_i2d:
      __ fcvt_d_w(f10, x10);
      break;
    case Bytecodes::_i2b:
      __ sext(x10, x10, 8);
      break;
    case Bytecodes::_i2c:
      __ zext(x10, x10, 16);
      break;
    case Bytecodes::_i2s:
      __ sext(x10, x10, 16);
      break;
    case Bytecodes::_l2i:
      __ sext(x10, x10, 32);
      break;
    case Bytecodes::_l2f:
      __ fcvt_s_l(f10, x10);
      break;
    case Bytecodes::_l2d:
      __ fcvt_d_l(f10, x10);
      break;
    case Bytecodes::_f2i:
      __ fcvt_w_s_safe(x10, f10);
      break;
    case Bytecodes::_f2l:
      __ fcvt_l_s_safe(x10, f10);
      break;
    case Bytecodes::_f2d:
      __ fcvt_d_s(f10, f10);
      break;
    case Bytecodes::_d2i:
      __ fcvt_w_d_safe(x10, f10);
      break;
    case Bytecodes::_d2l:
      __ fcvt_l_d_safe(x10, f10);
      break;
    case Bytecodes::_d2f:
      __ fcvt_s_d(f10, f10);
      break;
    default:
      ShouldNotReachHere();
  }
}

// -------------------------------------------------------------
// Comparisons and Branches
// -------------------------------------------------------------

void TemplateTable::lcmp() {
  transition(ltos, itos);
  __ pop_l(x11);
  __ cmp_l2i(t0, x11, x10);
  __ mv(x10, t0);
}

void TemplateTable::float_cmp(bool is_float, int unordered_result) {
  if (is_float) {
    __ pop_f(f11);
    __ float_compare(x10, f11, f10, unordered_result);
  } else {
    __ pop_d(f11);
    __ double_compare(x10, f11, f10, unordered_result);
  }
}

void TemplateTable::count_calls(Register method, Register temp) {
}

void TemplateTable::branch(bool is_jsr, bool is_wide) {
  __ profile_taken_branch(x10);
  const ByteSize be_offset = MethodCounters::backedge_counter_offset() +
                             InvocationCounter::counter_offset();

  // load branch displacement
  if (!is_wide) {
    __ lb(x12, at_bcp(1));
    __ lbu(t1, at_bcp(2));
    __ slli(x12, x12, 8);
    __ orr(x12, x12, t1);
  } else {
    __ lwu(x12, at_bcp(1));
    __ revbw(x12, x12);
  }

  if (is_jsr) {
    __ ld(t1, Address(xmethod, Method::const_offset()));
    __ addi(t1, t1, in_bytes(ConstMethod::codes_offset()) - (is_wide ? 5 : 3));
    __ sub(x11, xbcp, t1);
    __ push_i(x11);
    __ add(xbcp, xbcp, x12);
    __ lbu(t0, Address(xbcp, 0));
    __ dispatch_only(vtos);
    return;
  }

  __ add(xbcp, xbcp, x12);

  Label backedge_counter_overflow;
  Label dispatch;
  if (UseLoopCounter) {
    __ bgtz(x12, dispatch);

    Label has_counters;
    __ ld(t0, Address(xmethod, Method::method_counters_offset()));
    __ bnez(t0, has_counters);
    __ push_ptr(x10);
    __ push_ptr(x12);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::build_method_counters), xmethod);
    __ pop_ptr(x12);
    __ pop_ptr(x10);
    __ ld(t0, Address(xmethod, Method::method_counters_offset()));
    __ beqz(t0, dispatch);
    __ bind(has_counters);

    Label no_mdo;
    int increment = InvocationCounter::count_increment;
    int mask = ((1 << Tier0BackedgeNotifyFreqLog) - 1) << InvocationCounter::count_shift;
    if (ProfileInterpreter) {
      __ ld(x11, Address(xmethod, in_bytes(Method::method_data_offset())));
      __ beqz(x11, no_mdo);
      const Address mdo_backedge_counter(x11, in_bytes(MethodData::backedge_counter_offset()) +
                                         in_bytes(InvocationCounter::counter_offset()));
      __ increment_mask_and_jump(mdo_backedge_counter, increment, mask,
                                 x10, t0, false,
                                 UseOnStackReplacement ? &backedge_counter_overflow : &dispatch);
      __ j(dispatch);
    }
    __ bind(no_mdo);
    __ ld(t0, Address(xmethod, Method::method_counters_offset()));
    __ increment_mask_and_jump(Address(t0, be_offset), increment, mask,
                               x10, t1, false,
                               UseOnStackReplacement ? &backedge_counter_overflow : &dispatch);
    __ bind(dispatch);
  }

  __ lbu(t0, Address(xbcp, 0));
  __ dispatch_only(vtos);

  if (UseLoopCounter && UseOnStackReplacement) {
    __ bind(backedge_counter_overflow);
    __ neg(x12, x12);
    __ add(x12, x12, xbcp);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::frequency_counter_overflow), x12);
    __ lbu(t0, Address(xbcp, 0));

    Label no_osr;
    __ beqz(x10, no_osr);
    __ andi(sp, esp, -16);
    __ ld(t1, Address(x10, nmethod::osr_entry_point_offset()));
    __ jr(t1);

    __ bind(no_osr);
    __ dispatch_only(vtos);
  }
}

void TemplateTable::if_0cmp(Condition cc) {
  transition(itos, vtos);
  Label not_taken;
  __ sext(x10, x10, 32);
  switch (cc) {
    case equal:         __ bnez(x10, not_taken); break;
    case not_equal:     __ beqz(x10, not_taken); break;
    case less:          __ bgez(x10, not_taken); break;
    case less_equal:    __ bgtz(x10, not_taken); break;
    case greater:       __ blez(x10, not_taken); break;
    case greater_equal: __ bltz(x10, not_taken); break;
    default:            ShouldNotReachHere();
  }
  branch(false, false);
  __ bind(not_taken);
  __ profile_not_taken_branch(x10);
}

void TemplateTable::if_icmp(Condition cc) {
  transition(itos, vtos);
  Label not_taken;
  __ pop_i(x11);
  __ sext(x10, x10, 32);
  __ sext(x11, x11, 32);
  switch (cc) {
    case equal:         __ bne(x11, x10, not_taken); break;
    case not_equal:     __ beq(x11, x10, not_taken); break;
    case less:          __ bge(x11, x10, not_taken); break;
    case less_equal:    __ bgt(x11, x10, not_taken); break;
    case greater:       __ ble(x11, x10, not_taken); break;
    case greater_equal: __ blt(x11, x10, not_taken); break;
    default:            ShouldNotReachHere();
  }
  branch(false, false);
  __ bind(not_taken);
  __ profile_not_taken_branch(x10);
}

void TemplateTable::if_nullcmp(Condition cc) {
  transition(atos, vtos);
  Label not_taken;
  if (cc == equal) {
    __ bnez(x10, not_taken);
  } else {
    __ beqz(x10, not_taken);
  }
  branch(false, false);
  __ bind(not_taken);
  __ profile_not_taken_branch(x10);
}

void TemplateTable::if_acmp(Condition cc) {
  transition(atos, vtos);
  Label not_taken;
  __ pop_ptr(x11);
  if (cc == equal) {
    __ bne(x11, x10, not_taken);
  } else {
    __ beq(x11, x10, not_taken);
  }
  branch(false, false);
  __ bind(not_taken);
  __ profile_not_taken_branch(x10);
}

void TemplateTable::ret() {
  transition(vtos, vtos);
  locals_index(x11);
  __ ld(x11, aaddress(x11, t1, _masm));
  __ profile_ret(x11, x12);
  __ ld(xbcp, Address(xmethod, Method::const_offset()));
  __ add(xbcp, xbcp, x11);
  __ addi(xbcp, xbcp, in_bytes(ConstMethod::codes_offset()));
  __ dispatch_next(vtos, 0);
}

void TemplateTable::wide_ret() {
  transition(vtos, vtos);
  locals_index_wide(x11);
  __ ld(x11, aaddress(x11, t0, _masm));
  __ profile_ret(x11, x12);
  __ ld(xbcp, Address(xmethod, Method::const_offset()));
  __ add(xbcp, xbcp, x11);
  __ addi(xbcp, xbcp, in_bytes(ConstMethod::codes_offset()));
  __ dispatch_next(vtos, 0);
}

void TemplateTable::tableswitch() {
  Label default_case, continue_execution;
  transition(itos, vtos);
  // align xbcp
  __ la(x11, at_bcp(BytesPerInt));
  __ andi(x11, x11, -BytesPerInt);
  // load lo & hi
  __ lwu(x12, Address(x11, BytesPerInt));
  __ lwu(x13, Address(x11, 2 * BytesPerInt));
  __ revbw(x12, x12);
  __ revbw(x13, x13);
  // check against lo & hi
  __ blt(x10, x12, default_case);
  __ bgt(x10, x13, default_case);
  // lookup dispatch offset
  __ subw(x10, x10, x12);
  __ shift_left_add(x13, x10, x11, 2);
  __ lwu(x13, Address(x13, 3 * BytesPerInt));
  __ profile_switch_case(x10, x11, x12);
  // continue execution
  __ bind(continue_execution);
  __ revbw(x13, x13);
  __ add(xbcp, xbcp, x13);
  __ lbu(t0, Address(xbcp, 0));
  __ dispatch_only(vtos);
  // handle default
  __ bind(default_case);
  __ profile_switch_default(x10);
  __ lwu(x13, Address(x11, 0));
  __ j(continue_execution);
}

void TemplateTable::lookupswitch() {
  transition(itos, itos);
  __ stop("lookupswitch bytecode should have been rewritten");
}

void TemplateTable::fast_linearswitch() {
  transition(itos, vtos);
  Label loop_entry, loop, found, continue_execution;
  __ revbw(x10, x10);
  __ la(x14, at_bcp(BytesPerInt));
  __ andi(x14, x14, -BytesPerInt);
  __ lwu(x11, Address(x14, BytesPerInt));
  __ revbw(x11, x11);
  __ j(loop_entry);

  __ bind(loop);
  __ shift_left_add(t0, x11, x14, 3);
  __ lw(t0, Address(t0, 2 * BytesPerInt));
  __ beq(x10, t0, found);

  __ bind(loop_entry);
  __ subi(x11, x11, 1);
  __ bgez(x11, loop);

  // default case
  __ profile_switch_default(x10);
  __ lwu(x13, Address(x14, 0));
  __ j(continue_execution);

  // entry found
  __ bind(found);
  __ shift_left_add(t0, x11, x14, 3);
  __ lwu(x13, Address(t0, 3 * BytesPerInt));
  __ profile_switch_case(x11, x10, x14);

  __ bind(continue_execution);
  __ revbw(x13, x13);
  __ add(xbcp, xbcp, x13);
  __ lbu(t0, Address(xbcp, 0));
  __ dispatch_only(vtos);
}

void TemplateTable::fast_binaryswitch() {
  transition(itos, vtos);
  const Register key   = x10;
  const Register array = x11;
  const Register i     = x12;
  const Register j     = x13;
  const Register h     = x14;
  const Register temp  = x15;

  __ la(array, at_bcp(3 * BytesPerInt));
  __ andi(array, array, -BytesPerInt);

  __ mv(i, zr);
  __ lwu(j, Address(array, -BytesPerInt));
  __ revbw(j, j);

  Label entry;
  __ j(entry);

  {
    Label loop;
    __ bind(loop);
    __ addw(h, i, j);
    __ srliw(h, h, 1);
    __ shift_left_add(temp, h, array, 3);
    __ lwu(temp, Address(temp, 0));
    __ revbw(temp, temp);

    Label L_done, L_greater;
    __ bge(key, temp, L_greater);
    __ mv(j, h);
    __ j(L_done);
    __ bind(L_greater);
    __ mv(i, h);
    __ bind(L_done);

    __ bind(entry);
    __ addiw(h, i, 1);
    __ blt(h, j, loop);
  }

  Label default_case;
  __ shift_left_add(temp, i, array, 3);
  __ lwu(temp, Address(temp, 0));
  __ revbw(temp, temp);
  __ bne(key, temp, default_case);

  __ shift_left_add(temp, i, array, 3);
  __ lwu(j, Address(temp, BytesPerInt));
  __ profile_switch_case(i, key, array);
  __ revbw(j, j);
  __ add(xbcp, xbcp, j);
  __ lbu(t0, Address(xbcp, 0));
  __ dispatch_only(vtos);

  __ bind(default_case);
  __ profile_switch_default(i);
  __ lwu(j, Address(array, -2 * BytesPerInt));
  __ revbw(j, j);
  __ add(xbcp, xbcp, j);
  __ lbu(t0, Address(xbcp, 0));
  __ dispatch_only(vtos);
}

void TemplateTable::_return(TosState state) {
  transition(state, state);
  assert(_desc->calls_vm(), "inconsistent calls_vm information");

  if (_desc->bytecode() == Bytecodes::_return_register_finalizer) {
    assert(state == vtos, "only valid state");
    __ ld(c_rarg1, aaddress(0));
    __ load_klass(x13, c_rarg1);
    __ lwu(x13, Address(x13, Klass::access_flags_offset()));
    __ test_bit(t0, x13, exact_log2(JVM_ACC_HAS_FINALIZER));
    Label skip_register_finalizer;
    __ beqz(t0, skip_register_finalizer);

    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::register_finalizer), c_rarg1);
    __ bind(skip_register_finalizer);
  }

  if (_desc->bytecode() == Bytecodes::_return) {
    __ membar(MacroAssembler::StoreStore);
  }

  if (state == itos) {
    __ narrow(x10);
  }

  __ remove_activation(state);
  __ ret();
}

// ============================================================================
// Constant Pool Cache Resolution Helpers
// ============================================================================

void TemplateTable::resolve_cache_and_index(int byte_no,
                                            Register Rcache,
                                            Register index,
                                            size_t index_size) {
  const Register temp = x9;
  assert_different_registers(Rcache, index, temp);

  Label resolved;
  assert(byte_no == f1_byte || byte_no == f2_byte, "byte_no out of range");
  __ get_cache_and_index_and_bytecode_at_bcp(Rcache, index, temp, byte_no, 1, index_size);
  __ subi(t0, temp, (int)bytecode());
  __ beqz(t0, resolved);

  // resolve first time through
  address entry;
  switch (bytecode()) {
    case Bytecodes::_getstatic:
    case Bytecodes::_putstatic:
    case Bytecodes::_getfield:
    case Bytecodes::_putfield:
      entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_get_put);
      break;
    case Bytecodes::_invokevirtual:
    case Bytecodes::_invokespecial:
    case Bytecodes::_invokestatic:
    case Bytecodes::_invokeinterface:
      entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_invoke);
      break;
    case Bytecodes::_invokehandle:
      entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_invokehandle);
      break;
    case Bytecodes::_invokedynamic:
      entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_invokedynamic);
      break;
    default:
      fatal(err_msg("unexpected bytecode: %s", Bytecodes::name(bytecode())));
      break;
  }
  __ mv(temp, (int)bytecode());
  __ call_VM(noreg, entry, temp);

  // Update registers with resolved info
  __ get_cache_and_index_at_bcp(Rcache, index, 1, index_size);
  __ bind(resolved);
}

void TemplateTable::load_field_cp_cache_entry(Register obj,
                                              Register cache,
                                              Register index,
                                              Register off,
                                              Register flags,
                                              bool is_static) {
  assert_different_registers(cache, index, flags, off);

  ByteSize cp_base_offset = ConstantPoolCache::base_offset();
  // Field offset
  __ ld(off, Address(cache, in_bytes(cp_base_offset + ConstantPoolCacheEntry::f2_offset())));
  // Flags
  __ lwu(flags, Address(cache, in_bytes(cp_base_offset + ConstantPoolCacheEntry::flags_offset())));

  // klass overwrite register
  if (is_static) {
    __ ld(obj, Address(cache, in_bytes(cp_base_offset + ConstantPoolCacheEntry::f1_offset())));
    const int mirror_offset = in_bytes(Klass::java_mirror_offset());
    __ ld(obj, Address(obj, mirror_offset));
  }
}

void TemplateTable::load_invoke_cp_cache_entry(int byte_no,
                                               Register method,
                                               Register itable_index,
                                               Register flags,
                                               bool is_invokevirtual,
                                               bool is_invokevfinal,
                                               bool is_invokedynamic) {
  const Register cache = x12;
  const Register index = x14;
  assert_different_registers(method, flags);
  assert_different_registers(method, cache, index);
  assert_different_registers(itable_index, flags);
  assert_different_registers(itable_index, cache, index);

  assert(is_invokevirtual == (byte_no == f2_byte), "is_invokevirtual flag redundant");
  const int method_offset = in_bytes(
    ConstantPoolCache::base_offset() +
      (is_invokevirtual
       ? ConstantPoolCacheEntry::f2_offset()
       : ConstantPoolCacheEntry::f1_offset()));
  const int flags_offset = in_bytes(ConstantPoolCache::base_offset() +
                                    ConstantPoolCacheEntry::flags_offset());
  const int index_offset = in_bytes(ConstantPoolCache::base_offset() +
                                    ConstantPoolCacheEntry::f2_offset());

  size_t index_size = (is_invokedynamic ? sizeof(u4) : sizeof(u2));
  resolve_cache_and_index(byte_no, cache, index, index_size);
  __ ld(method, Address(cache, method_offset));

  if (itable_index != noreg) {
    __ ld(itable_index, Address(cache, index_offset));
  }
  __ lwu(flags, Address(cache, flags_offset));
}

void TemplateTable::jvmti_post_field_access(Register cache, Register index,
                                            bool is_static, bool has_tos) {
  if (JvmtiExport::can_post_field_access()) {
    Label L1;
    assert_different_registers(cache, index, x10);
    __ la(t0, ExternalAddress((address) JvmtiExport::get_field_access_count_addr()));
    __ lwu(x10, Address(t0, 0));
    __ beqz(x10, L1);

    __ get_cache_and_index_at_bcp(c_rarg2, c_rarg3, 1);
    __ addi(c_rarg2, c_rarg2, in_bytes(ConstantPoolCache::base_offset()));

    if (is_static) {
      __ mv(c_rarg1, zr);
    } else {
      __ ld(c_rarg1, at_tos());
      __ verify_oop(c_rarg1);
    }
    __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                                       InterpreterRuntime::post_field_access),
               c_rarg1, c_rarg2, c_rarg3);
    __ get_cache_and_index_at_bcp(cache, index, 1);
    __ bind(L1);
  }
}

void TemplateTable::pop_and_check_object(Register r) {
  __ pop_ptr(r);
  __ null_check(r);
  __ verify_oop(r);
}

// ============================================================================
// Field Access Bytecodes
// ============================================================================

void TemplateTable::getfield_or_static(int byte_no, bool is_static) {
  const Register cache     = x12;
  const Register index     = x13;
  const Register obj       = x14;
  const Register off       = x9;
  const Register flags     = x10;
  const Register raw_flags = x15;
  const Register bc        = x14; // uses same reg as obj

  resolve_cache_and_index(byte_no, cache, index, sizeof(u2));
  jvmti_post_field_access(cache, index, is_static, false);
  load_field_cp_cache_entry(obj, cache, index, off, raw_flags, is_static);

  if (!is_static) {
    pop_and_check_object(obj);
  }

  __ add(off, obj, off);
  const Address field(off, 0);

  Label Done, notByte, notBool, notInt, notShort, notChar,
              notLong, notFloat, notObj, notDouble;

  __ srliw(flags, raw_flags, ConstantPoolCacheEntry::tos_state_shift);
  __ andi(flags, flags, (1 << ConstantPoolCacheEntry::tos_state_bits) - 1);

  assert(btos == 0, "change code, btos != 0");
  __ bnez(flags, notByte);

  // btos
  __ lb(x10, field);
  __ push(btos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_bgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notByte);
  __ subi(t0, flags, ztos);
  __ bnez(t0, notBool);

  // ztos
  __ lbu(x10, field);
  __ push(ztos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_bgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notBool);
  __ subi(t0, flags, atos);
  __ bnez(t0, notObj);

  // atos
  __ load_heap_oop(x10, field);
  __ push(atos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_agetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notObj);
  __ subi(t0, flags, itos);
  __ bnez(t0, notInt);

  // itos
  __ lw(x10, field);
  __ push(itos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_igetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notInt);
  __ subi(t0, flags, ctos);
  __ bnez(t0, notChar);

  // ctos
  __ lhu(x10, field);
  __ push(ctos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_cgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notChar);
  __ subi(t0, flags, stos);
  __ bnez(t0, notShort);

  // stos
  __ lh(x10, field);
  __ push(stos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_sgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notShort);
  __ subi(t0, flags, ltos);
  __ bnez(t0, notLong);

  // ltos
  __ ld(x10, field);
  __ push(ltos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_lgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notLong);
  __ subi(t0, flags, ftos);
  __ bnez(t0, notFloat);

  // ftos
  __ flw(f10, field);
  __ push(ftos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_fgetfield, bc, x11);
  }
  __ j(Done);

  __ bind(notFloat);
#ifdef ASSERT
  __ subi(t0, flags, dtos);
  __ bnez(t0, notDouble);
#endif

  // dtos
  __ fld(f10, field);
  __ push(dtos);
  if (!is_static) {
    patch_bytecode(Bytecodes::_fast_dgetfield, bc, x11);
  }

#ifdef ASSERT
  __ j(Done);
  __ bind(notDouble);
  __ stop("Bad state");
#endif

  __ bind(Done);

  Label notVolatile;
  __ test_bit(t0, raw_flags, ConstantPoolCacheEntry::is_volatile_shift);
  __ beqz(t0, notVolatile);
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ bind(notVolatile);
}

void TemplateTable::getfield(int byte_no) {
  getfield_or_static(byte_no, false);
}

void TemplateTable::getstatic(int byte_no) {
  getfield_or_static(byte_no, true);
}

void TemplateTable::jvmti_post_field_mod(Register cache, Register index, bool is_static) {
  transition(vtos, vtos);

  if (JvmtiExport::can_post_field_modification()) {
    Label L1;
    assert_different_registers(cache, index, x10);
    __ la(t0, ExternalAddress((address) JvmtiExport::get_field_modification_count_addr()));
    __ lwu(x10, Address(t0, 0));
    __ beqz(x10, L1);

    __ get_cache_and_index_at_bcp(c_rarg2, c_rarg3, 1);
    __ addi(c_rarg2, c_rarg2, in_bytes(ConstantPoolCache::base_offset()));

    if (is_static) {
      __ mv(c_rarg1, zr);
    } else {
      __ lwu(c_rarg3, Address(c_rarg2, in_bytes(ConstantPoolCacheEntry::flags_offset())));
      __ srliw(c_rarg3, c_rarg3, ConstantPoolCacheEntry::tos_state_shift);
      __ andi(c_rarg3, c_rarg3, (1 << ConstantPoolCacheEntry::tos_state_bits) - 1);

      Label nope2, ok;
      __ ld(c_rarg1, at_tos_p1());   // initially assume a one word jvalue
      __ subi(t0, c_rarg3, ltos);
      __ beqz(t0, ok);
      __ subi(t0, c_rarg3, dtos);
      __ bnez(t0, nope2);
      __ bind(ok);
      __ ld(c_rarg1, at_tos_p2());   // ltos/dtos: two word jvalue
      __ bind(nope2);
    }

    __ mv(c_rarg3, esp);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_modification),
               c_rarg1, c_rarg2, c_rarg3);
    __ get_cache_and_index_at_bcp(cache, index, 1);
    __ bind(L1);
  }
}

void TemplateTable::putfield_or_static(int byte_no, bool is_static) {
  transition(vtos, vtos);

  const Register cache = x12;
  const Register index = x13;
  const Register obj   = x12;
  const Register off   = x9;
  const Register flags = x10;
  const Register bc    = x14;

  resolve_cache_and_index(byte_no, cache, index, sizeof(u2));
  jvmti_post_field_mod(cache, index, is_static);
  load_field_cp_cache_entry(obj, cache, index, off, flags, is_static);

  Label Done;
  __ mv(x15, flags);

  {
    Label notVolatile;
    __ test_bit(t0, x15, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::StoreStore | MacroAssembler::LoadStore);
    __ bind(notVolatile);
  }

  Label notByte, notBool, notInt, notShort, notChar,
        notLong, notFloat, notObj, notDouble;

  __ srliw(flags, flags, ConstantPoolCacheEntry::tos_state_shift);
  __ andi(flags, flags, (1 << ConstantPoolCacheEntry::tos_state_bits) - 1);

  assert(btos == 0, "change code, btos != 0");
  __ bnez(flags, notByte);

  // btos
  {
    __ pop(btos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ sb(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_bputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notByte);
  __ subi(t0, flags, ztos);
  __ bnez(t0, notBool);

  // ztos
  {
    __ pop(ztos);
    if (!is_static) pop_and_check_object(obj);
    __ andi(x10, x10, 0x1);
    __ add(off, obj, off);
    __ sb(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_zputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notBool);
  __ subi(t0, flags, atos);
  __ bnez(t0, notObj);

  // atos
  {
    __ pop(atos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    do_oop_store(_masm, Address(off, 0), x10, _bs->kind(), false);
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_aputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notObj);
  __ subi(t0, flags, itos);
  __ bnez(t0, notInt);

  // itos
  {
    __ pop(itos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ sw(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_iputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notInt);
  __ subi(t0, flags, ctos);
  __ bnez(t0, notChar);

  // ctos
  {
    __ pop(ctos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ sh(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_cputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notChar);
  __ subi(t0, flags, stos);
  __ bnez(t0, notShort);

  // stos
  {
    __ pop(stos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ sh(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_sputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notShort);
  __ subi(t0, flags, ltos);
  __ bnez(t0, notLong);

  // ltos
  {
    __ pop(ltos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ sd(x10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_lputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notLong);
  __ subi(t0, flags, ftos);
  __ bnez(t0, notFloat);

  // ftos
  {
    __ pop(ftos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ fsw(f10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_fputfield, bc, x11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notFloat);
#ifdef ASSERT
  __ subi(t0, flags, dtos);
  __ bnez(t0, notDouble);
#endif

  // dtos
  {
    __ pop(dtos);
    if (!is_static) pop_and_check_object(obj);
    __ add(off, obj, off);
    __ fsd(f10, Address(off, 0));
    if (!is_static) {
      patch_bytecode(Bytecodes::_fast_dputfield, bc, x11, true, byte_no);
    }
  }

#ifdef ASSERT
  __ j(Done);
  __ bind(notDouble);
  __ stop("Bad state");
#endif

  __ bind(Done);

  {
    Label notVolatile;
    __ test_bit(t0, x15, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::StoreLoad | MacroAssembler::StoreStore);
    __ bind(notVolatile);
  }
}

void TemplateTable::putfield(int byte_no) {
  putfield_or_static(byte_no, false);
}

void TemplateTable::putstatic(int byte_no) {
  putfield_or_static(byte_no, true);
}

void TemplateTable::jvmti_post_fast_field_mod() {
  if (JvmtiExport::can_post_field_modification()) {
    Label L2;
    __ la(t0, ExternalAddress((address) JvmtiExport::get_field_modification_count_addr()));
    __ lwu(c_rarg3, Address(t0, 0));
    __ beqz(c_rarg3, L2);

    __ pop_ptr(x9);                  // copy object pointer from tos
    __ verify_oop(x9);
    __ push_ptr(x9);                 // put object pointer back on tos

    switch (bytecode()) {
      case Bytecodes::_fast_aputfield: __ push_ptr(x10); break;
      case Bytecodes::_fast_bputfield:
      case Bytecodes::_fast_zputfield:
      case Bytecodes::_fast_sputfield:
      case Bytecodes::_fast_cputfield:
      case Bytecodes::_fast_iputfield: __ push_i(x10); break;
      case Bytecodes::_fast_dputfield: __ push_d(); break;
      case Bytecodes::_fast_fputfield: __ push_f(); break;
      case Bytecodes::_fast_lputfield: __ push_l(x10); break;
      default:
        ShouldNotReachHere();
    }
    __ mv(c_rarg3, esp);             // points to jvalue on stack
    __ get_cache_entry_pointer_at_bcp(c_rarg2, x10, 1);
    __ verify_oop(x9);

    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_modification),
               x9, c_rarg2, c_rarg3);

    switch (bytecode()) {
      case Bytecodes::_fast_aputfield: __ pop_ptr(x10); break;
      case Bytecodes::_fast_bputfield:
      case Bytecodes::_fast_zputfield:
      case Bytecodes::_fast_sputfield:
      case Bytecodes::_fast_cputfield:
      case Bytecodes::_fast_iputfield: __ pop_i(x10); break;
      case Bytecodes::_fast_dputfield: __ pop_d(); break;
      case Bytecodes::_fast_fputfield: __ pop_f(); break;
      case Bytecodes::_fast_lputfield: __ pop_l(x10); break;
      default: break;
    }
    __ bind(L2);
  }
}

void TemplateTable::fast_storefield(TosState state) {
  transition(state, vtos);

  ByteSize base = ConstantPoolCache::base_offset();

  jvmti_post_fast_field_mod();

  // access constant pool cache
  __ get_cache_and_index_at_bcp(x12, x11, 1);

  // Must prevent reordering of following cp cache loads with bytecode load
  __ membar(MacroAssembler::LoadLoad);

  // test for volatile with x13
  __ lwu(x13, Address(x12, in_bytes(base + ConstantPoolCacheEntry::flags_offset())));

  // replace index with field offset from cache entry
  __ ld(x11, Address(x12, in_bytes(base + ConstantPoolCacheEntry::f2_offset())));

  {
    Label notVolatile;
    __ test_bit(t0, x13, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::StoreStore | MacroAssembler::LoadStore);
    __ bind(notVolatile);
  }

  // Get object from stack
  pop_and_check_object(x12);

  // field address
  __ add(x11, x12, x11);
  const Address field(x11, 0);

  // access field
  switch (bytecode()) {
    case Bytecodes::_fast_aputfield:
      do_oop_store(_masm, field, x10, _bs->kind(), false);
      break;
    case Bytecodes::_fast_lputfield:
      __ sd(x10, field);
      break;
    case Bytecodes::_fast_iputfield:
      __ sw(x10, field);
      break;
    case Bytecodes::_fast_zputfield:
      __ andi(x10, x10, 0x1);
      __ sb(x10, field);
      break;
    case Bytecodes::_fast_bputfield:
      __ sb(x10, field);
      break;
    case Bytecodes::_fast_sputfield:
    case Bytecodes::_fast_cputfield:
      __ sh(x10, field);
      break;
    case Bytecodes::_fast_fputfield:
      __ fsw(f10, field);
      break;
    case Bytecodes::_fast_dputfield:
      __ fsd(f10, field);
      break;
    default:
      ShouldNotReachHere();
  }

  {
    Label notVolatile;
    __ test_bit(t0, x13, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::StoreLoad | MacroAssembler::StoreStore);
    __ bind(notVolatile);
  }
}

void TemplateTable::fast_accessfield(TosState state) {
  transition(atos, state);

  if (JvmtiExport::can_post_field_access()) {
    Label L1;
    __ la(t0, ExternalAddress((address) JvmtiExport::get_field_access_count_addr()));
    __ lwu(x12, Address(t0, 0));
    __ beqz(x12, L1);

    __ get_cache_entry_pointer_at_bcp(c_rarg2, t1, 1);
    __ verify_oop(x10);
    __ push_ptr(x10);
    __ mv(c_rarg1, x10);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_access),
               c_rarg1, c_rarg2);
    __ pop_ptr(x10);
    __ bind(L1);
  }

  __ get_cache_and_index_at_bcp(x12, x11, 1);

  __ membar(MacroAssembler::LoadLoad);

  __ ld(x11, Address(x12, in_bytes(ConstantPoolCache::base_offset() +
                                  ConstantPoolCacheEntry::f2_offset())));
  __ lwu(x13, Address(x12, in_bytes(ConstantPoolCache::base_offset() +
                                   ConstantPoolCacheEntry::flags_offset())));

  __ verify_oop(x10);
  __ null_check(x10);
  __ add(x11, x10, x11);
  const Address field(x11, 0);

  switch (bytecode()) {
    case Bytecodes::_fast_agetfield:
      __ load_heap_oop(x10, field);
      __ verify_oop(x10);
      break;
    case Bytecodes::_fast_lgetfield:
      __ ld(x10, field);
      break;
    case Bytecodes::_fast_igetfield:
      __ lw(x10, field);
      break;
    case Bytecodes::_fast_bgetfield:
      __ lb(x10, field);
      break;
    case Bytecodes::_fast_sgetfield:
      __ lh(x10, field);
      break;
    case Bytecodes::_fast_cgetfield:
      __ lhu(x10, field);
      break;
    case Bytecodes::_fast_fgetfield:
      __ flw(f10, field);
      break;
    case Bytecodes::_fast_dgetfield:
      __ fld(f10, field);
      break;
    default:
      ShouldNotReachHere();
  }

  {
    Label notVolatile;
    __ test_bit(t0, x13, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
    __ bind(notVolatile);
  }
}

void TemplateTable::fast_xaccess(TosState state) {
  transition(vtos, state);

  // get receiver
  __ ld(x10, aaddress(0));
  // access constant pool cache
  __ get_cache_and_index_at_bcp(x12, x13, 2);
  __ ld(x11, Address(x12, in_bytes(ConstantPoolCache::base_offset() +
                                  ConstantPoolCacheEntry::f2_offset())));

  __ addi(xbcp, xbcp, 1);
  __ null_check(x10);
  __ add(x11, x10, x11);
  const Address field(x11, 0);

  switch (state) {
    case itos:
      __ lw(x10, field);
      break;
    case atos:
      __ load_heap_oop(x10, field);
      __ verify_oop(x10);
      break;
    case ftos:
      __ flw(f10, field);
      break;
    default:
      ShouldNotReachHere();
  }

  {
    Label notVolatile;
    __ lwu(x13, Address(x12, in_bytes(ConstantPoolCache::base_offset() +
                                     ConstantPoolCacheEntry::flags_offset())));
    __ test_bit(t0, x13, ConstantPoolCacheEntry::is_volatile_shift);
    __ beqz(t0, notVolatile);
    __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
    __ bind(notVolatile);
  }

  __ subi(xbcp, xbcp, 1);
}

// ============================================================================
// Method Invocations
// ============================================================================

void TemplateTable::prepare_invoke(int byte_no,
                                   Register method,
                                   Register index,
                                   Register recv,
                                   Register flags) {
  Bytecodes::Code code = bytecode();
  const bool is_invokeinterface  = code == Bytecodes::_invokeinterface;
  const bool is_invokedynamic    = code == Bytecodes::_invokedynamic;
  const bool is_invokehandle     = code == Bytecodes::_invokehandle;
  const bool is_invokevirtual    = code == Bytecodes::_invokevirtual;
  const bool load_receiver       = (recv  != noreg);
  const bool save_flags          = (flags != noreg);
  assert(load_receiver == (code != Bytecodes::_invokestatic && code != Bytecodes::_invokedynamic), "");
  assert(save_flags    == (is_invokeinterface || is_invokevirtual), "need flags for vfinal");

  if (recv  == noreg)  recv  = x12;
  if (flags == noreg)  flags = x13;
  assert_different_registers(method, index, recv, flags);

  // save 'interpreter return address'
  __ save_bcp();

  load_invoke_cp_cache_entry(byte_no, method, index, flags, is_invokevirtual, false, is_invokedynamic);

  // maybe push appendix to arguments (just before return address)
  if (is_invokedynamic || is_invokehandle) {
    Label L_no_push;
    __ test_bit(t0, flags, ConstantPoolCacheEntry::has_appendix_shift);
    __ beqz(t0, L_no_push);
    assert(ConstantPoolCacheEntry::_indy_resolved_references_appendix_offset == 0, "appendix expected at index+0");
    __ load_resolved_reference_at_index(t0, index);
    __ push(t0);
    __ bind(L_no_push);
  }

  // load receiver if needed (note: no return address pushed yet)
  if (load_receiver) {
    __ andi(recv, flags, ConstantPoolCacheEntry::parameter_size_mask);
    __ slli(t0, recv, Interpreter::logStackElementSize);
    __ add(t0, esp, t0);
    __ ld(recv, Address(t0, -Interpreter::expr_offset_in_bytes(1)));
    __ verify_oop(recv);
  }

  // compute return type and load return address
  __ srliw(t1, flags, ConstantPoolCacheEntry::tos_state_shift);
  __ andi(t1, t1, (1 << ConstantPoolCacheEntry::tos_state_bits) - 1);
  const address table_addr = (address) Interpreter::invoke_return_entry_table_for(code);
  __ mv(t0, table_addr);
  __ slli(t1, t1, LogBytesPerWord);
  __ add(t0, t0, t1);
  __ ld(ra, Address(t0, 0));
}

void TemplateTable::invokevirtual_helper(Register index,
                                         Register recv,
                                         Register flags) {
  assert_different_registers(index, recv, x10, x13);
  Label notFinal;
  __ test_bit(t0, flags, ConstantPoolCacheEntry::is_vfinal_shift);
  __ beqz(t0, notFinal);

  const Register method = index;
  assert(method == xmethod, "Method must be xmethod for interpreter calling convention");

  __ null_check(recv);
  __ profile_final_call(x10);
  __ profile_arguments_type(x10, method, x14, true);
  __ jump_from_interpreted(method);

  __ bind(notFinal);
  __ null_check(recv, oopDesc::klass_offset_in_bytes());
  __ load_klass(x10, recv);
  __ profile_virtual_call(x10, xlocals, x13);
  __ lookup_virtual_method(x10, index, method);
  __ profile_arguments_type(x13, method, x14, true);
  __ jump_from_interpreted(method, x13);
}

void TemplateTable::invokevirtual(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f2_byte, "use this argument");

  prepare_invoke(byte_no, xmethod, noreg, x12, x13);
  invokevirtual_helper(xmethod, x12, x13);
}

void TemplateTable::invokespecial(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  prepare_invoke(byte_no, xmethod, noreg, x12);
  __ verify_oop(x12);
  __ null_check(x12);
  __ profile_call(x10);
  __ profile_arguments_type(x10, xmethod, xbcp, false);
  __ jump_from_interpreted(xmethod);
}

void TemplateTable::invokestatic(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  prepare_invoke(byte_no, xmethod);
  __ profile_call(x10);
  __ profile_arguments_type(x10, xmethod, x14, false);
  __ jump_from_interpreted(xmethod);
}

void TemplateTable::fast_invokevfinal(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f2_byte, "use this argument");
  __ should_not_reach_here();
}

void TemplateTable::invokeinterface(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  prepare_invoke(byte_no, x10, xmethod, x12, x13);

  // x10: interface klass (from f1)
  // xmethod: method (from f2)
  // x12: receiver
  // x13: flags

  Label notMethod;
  __ test_bit(t0, x13, ConstantPoolCacheEntry::is_forced_virtual_shift);
  __ beqz(t0, notMethod);

  invokevirtual_helper(xmethod, x12, x13);
  __ bind(notMethod);

  __ restore_locals();
  __ null_check(x12, oopDesc::klass_offset_in_bytes());
  __ load_klass(x13, x12);

  Label no_such_interface, no_such_method;

  __ lookup_interface_method(x13, x10, noreg,
                             t1, x30,
                             no_such_interface,
                             /*return_method=*/false);

  __ profile_virtual_call(x13, t1, x30);

  __ ld(x10, Address(xmethod, Method::const_offset()));
  __ ld(x10, Address(x10, ConstMethod::constants_offset()));
  __ ld(x10, Address(x10, ConstantPool::pool_holder_offset_in_bytes()));
  __ lwu(xmethod, Address(xmethod, Method::itable_index_offset()));
  __ subw(xmethod, xmethod, Method::itable_index_max);
  __ negw(xmethod, xmethod);

  __ lookup_interface_method(x13, x10, xmethod,
                             xmethod, x30,
                             no_such_interface);

  __ beqz(xmethod, no_such_method);
  __ profile_arguments_type(x13, xmethod, x30, true);
  __ jump_from_interpreted(xmethod, x13);
  __ should_not_reach_here();

  __ bind(no_such_method);
  __ restore_bcp();
  __ restore_locals();
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_AbstractMethodError));
  __ should_not_reach_here();

  __ bind(no_such_interface);
  __ restore_bcp();
  __ restore_locals();
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_IncompatibleClassChangeError));
  __ should_not_reach_here();
}

void TemplateTable::invokehandle(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  if (!EnableInvokeDynamic) {
    __ should_not_reach_here();
    return;
  }

  prepare_invoke(byte_no, xmethod, x10, x12);
  __ verify_method_ptr(x12);
  __ verify_oop(x12);
  __ null_check(x12);

  __ profile_final_call(x13);
  __ profile_arguments_type(x13, xmethod, x14, true);

  __ jump_from_interpreted(xmethod, x10);
}

void TemplateTable::invokedynamic(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  if (!EnableInvokeDynamic) {
    __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                     InterpreterRuntime::throw_IncompatibleClassChangeError));
    __ should_not_reach_here();
    return;
  }

  prepare_invoke(byte_no, xmethod, x10);

  __ profile_call(xbcp);
  __ profile_arguments_type(x13, xmethod, x14, false);
  __ verify_oop(x10);
  __ jump_from_interpreted(xmethod, x10);
}

// ============================================================================
// Allocation, Type Checking, Monitors, Wide, and Multi-Array
// ============================================================================

void TemplateTable::_new() {
  transition(vtos, atos);

  __ get_unsigned_2_byte_index_at_bcp(x13, 1);
  Label slow_case;
  Label done;
  Label initialize_header;
  Label initialize_object;

  __ get_cpool_and_tags(x14, x10);
  const int tags_offset = Array<u1>::base_offset_in_bytes();
  __ add(t1, x10, x13);
  __ addi(t1, t1, tags_offset);
  __ lbu(t1, Address(t1, 0));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ subi(t0, t1, JVM_CONSTANT_Class);
  __ bnez(t0, slow_case);

  // get InstanceKlass
  __ slli(t0, x13, LogBytesPerWord);
  __ add(x14, x14, t0);
  __ ld(x14, Address(x14, sizeof(ConstantPool)));

  // make sure klass is fully initialized
  __ lbu(t0, Address(x14, InstanceKlass::init_state_offset()));
  __ subi(t0, t0, InstanceKlass::fully_initialized);
  __ bnez(t0, slow_case);

  // get instance_size in InstanceKlass (scaled to a count of bytes)
  __ lwu(x13, Address(x14, Klass::layout_helper_offset()));
  __ test_bit(t0, x13, exact_log2(Klass::_lh_instance_slow_path_bit));
  __ bnez(t0, slow_case);

  if (UseTLAB) {
    __ tlab_allocate(x10, x13, 0, x11, x12, slow_case);

    if (ZeroTLAB) {
      __ j(initialize_header);
    } else {
      __ j(initialize_object);
    }
  }

  if (UseTLAB) {
    __ bind(initialize_object);
    __ subi(x13, x13, sizeof(oopDesc));
    __ beqz(x13, initialize_header);

    // Initialize object fields
    {
      __ addi(x12, x10, sizeof(oopDesc));
      Label loop;
      __ bind(loop);
      __ sd(zr, Address(x12, 0));
      __ addi(x12, x12, BytesPerLong);
      __ subi(x13, x13, BytesPerLong);
      __ bnez(x13, loop);
    }

    // initialize object header only.
    __ bind(initialize_header);
    if (UseBiasedLocking) {
      __ ld(t0, Address(x14, Klass::prototype_header_offset()));
    } else {
      __ mv(t0, (intptr_t)markOopDesc::prototype());
    }
    __ sd(t0, Address(x10, oopDesc::mark_offset_in_bytes()));
    __ store_klass_gap(x10, zr);
    __ store_klass(x10, x14);

    if (DTraceAllocProbes) {
      __ push(atos);
      __ call_VM_leaf(
           CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_object_alloc), x10);
      __ pop(atos);
    }
    __ j(done);
  }

  // slow case
  __ bind(slow_case);
  __ get_constant_pool(c_rarg1);
  __ get_unsigned_2_byte_index_at_bcp(c_rarg2, 1);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::_new), c_rarg1, c_rarg2);
  __ verify_oop(x10);

  // continue
  __ bind(done);
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::newarray() {
  transition(itos, atos);
  __ lbu(c_rarg1, at_bcp(1));
  __ mv(c_rarg2, x10);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::newarray),
          c_rarg1, c_rarg2);
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::anewarray() {
  transition(itos, atos);
  __ get_unsigned_2_byte_index_at_bcp(c_rarg2, 1);
  __ get_constant_pool(c_rarg1);
  __ mv(c_rarg3, x10);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::anewarray),
          c_rarg1, c_rarg2, c_rarg3);
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::arraylength() {
  transition(atos, itos);
  __ null_check(x10, arrayOopDesc::length_offset_in_bytes());
  __ lwu(x10, Address(x10, arrayOopDesc::length_offset_in_bytes()));
}

void TemplateTable::checkcast() {
  transition(atos, atos);
  Label done, is_null, ok_is_subtype, quicked, resolved;
  __ beqz(x10, is_null);

  // Get cpool & tags index
  __ get_cpool_and_tags(x12, x13); // x12=cpool, x13=tags array
  __ get_unsigned_2_byte_index_at_bcp(x9, 1); // x9=index

  // See if bytecode has already been quicked
  __ add(t0, x13, x9);
  __ addi(t0, t0, Array<u1>::base_offset_in_bytes());
  __ lbu(x11, Address(t0, 0));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ subi(t0, x11, JVM_CONSTANT_Class);
  __ beqz(t0, quicked);

  __ push(atos);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::quicken_io_cc));
  __ get_vm_result_2(x10, xthread);
  __ pop(x13); // restore receiver
  __ j(resolved);

  // Get superklass in x10 and subklass in x13
  __ bind(quicked);
  __ mv(x13, x10);
  __ slli(t0, x9, LogBytesPerWord);
  __ add(x10, x12, t0);
  __ ld(x10, Address(x10, sizeof(ConstantPool)));

  __ bind(resolved);
  __ load_klass(x9, x13);

  // Generate subtype check. Blows t0, t1.
  __ gen_subtype_check(x9, ok_is_subtype);

  // Come here on failure
  __ push(x13);
  __ j(Interpreter::_throw_ClassCastException_entry);

  // Come here on success
  __ bind(ok_is_subtype);
  __ mv(x10, x13);

  if (ProfileInterpreter) {
    __ j(done);
    __ bind(is_null);
    __ profile_null_seen(x12);
  } else {
    __ bind(is_null);
  }
  __ bind(done);
}

void TemplateTable::instanceof() {
  transition(atos, itos);
  Label done, is_null, ok_is_subtype, quicked, resolved;
  __ beqz(x10, is_null);

  // Get cpool & tags index
  __ get_cpool_and_tags(x12, x13); // x12=cpool, x13=tags array
  __ get_unsigned_2_byte_index_at_bcp(x9, 1); // x9=index

  // See if bytecode has already been quicked
  __ add(t0, x13, x9);
  __ addi(t0, t0, Array<u1>::base_offset_in_bytes());
  __ lbu(x11, Address(t0, 0));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ subi(t0, x11, JVM_CONSTANT_Class);
  __ beqz(t0, quicked);

  __ push(atos);
  call_VM(x10, CAST_FROM_FN_PTR(address, InterpreterRuntime::quicken_io_cc));
  __ get_vm_result_2(x10, xthread);
  __ pop(x13); // restore receiver
  __ verify_oop(x13);
  __ load_klass(x13, x13);
  __ j(resolved);

  // Get superklass in x10 and subklass in x13
  __ bind(quicked);
  __ load_klass(x13, x10);
  __ slli(t0, x9, LogBytesPerWord);
  __ add(x10, x12, t0);
  __ ld(x10, Address(x10, sizeof(ConstantPool)));

  __ bind(resolved);

  __ gen_subtype_check(x13, ok_is_subtype);

  __ mv(x10, zr);
  __ j(done);

  __ bind(ok_is_subtype);
  __ li(x10, 1);

  if (ProfileInterpreter) {
    __ j(done);
    __ bind(is_null);
    __ profile_null_seen(x12);
  } else {
    __ bind(is_null);
  }
  __ bind(done);
}

void TemplateTable::_breakpoint() {
  transition(vtos, vtos);

  __ get_method(c_rarg1);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::get_original_bytecode_at),
             c_rarg1, xbcp);
  __ mv(x9, x10);

  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address, InterpreterRuntime::_breakpoint),
             xmethod, xbcp);

  __ mv(t0, x9);
  __ dispatch_only_normal(vtos);
}

void TemplateTable::athrow() {
  transition(atos, vtos);
  __ null_check(x10);
  __ j(Interpreter::throw_exception_entry());
}

void TemplateTable::monitorenter() {
  transition(atos, vtos);

  __ null_check(x10);

  const Address monitor_block_top(
        fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  const Address monitor_block_bot(
        fp, frame::interpreter_frame_initial_sp_offset * wordSize);
  const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;

  Label allocated;

  __ mv(c_rarg1, zr);

  // find a free slot in monitor block
  {
    Label entry, loop, exit, notUsed;
    __ ld(c_rarg3, monitor_block_top);
    __ la(c_rarg2, monitor_block_bot);

    __ j(entry);

    __ bind(loop);
    __ ld(t0, Address(c_rarg3, BasicObjectLock::obj_offset_in_bytes()));
    __ bnez(t0, notUsed);
    __ mv(c_rarg1, c_rarg3);
    __ bind(notUsed);
    __ beq(x10, t0, exit);
    __ addi(c_rarg3, c_rarg3, entry_size);
    __ bind(entry);
    __ bne(c_rarg3, c_rarg2, loop);
    __ bind(exit);
  }

  __ bnez(c_rarg1, allocated);

  // allocate one if no free slot
  {
    Label entry, loop;
    __ ld(c_rarg1, monitor_block_bot);
    __ subi(esp, esp, entry_size);
    __ subi(c_rarg1, c_rarg1, entry_size);
    __ mv(c_rarg3, esp);
    __ sd(c_rarg1, monitor_block_bot);

    __ subi(sp, sp, entry_size);

    __ j(entry);
    __ bind(loop);
    __ ld(c_rarg2, Address(c_rarg3, entry_size));
    __ sd(c_rarg2, Address(c_rarg3, 0));
    __ addi(c_rarg3, c_rarg3, wordSize);
    __ bind(entry);
    __ bne(c_rarg3, c_rarg1, loop);
  }

  __ bind(allocated);
  __ addi(xbcp, xbcp, 1);
  __ sd(x10, Address(c_rarg1, BasicObjectLock::obj_offset_in_bytes()));
  __ lock_object(c_rarg1);
  __ save_bcp();
  __ generate_stack_overflow_check(0);
  __ dispatch_next(vtos);
}

void TemplateTable::monitorexit() {
  transition(atos, vtos);

  __ null_check(x10);

  const Address monitor_block_top(
        fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  const Address monitor_block_bot(
        fp, frame::interpreter_frame_initial_sp_offset * wordSize);
  const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;

  Label found;

  // find matching slot
  {
    Label entry, loop;
    __ ld(c_rarg1, monitor_block_top);
    __ la(c_rarg2, monitor_block_bot);
    __ j(entry);

    __ bind(loop);
    __ ld(t0, Address(c_rarg1, BasicObjectLock::obj_offset_in_bytes()));
    __ beq(x10, t0, found);
    __ addi(c_rarg1, c_rarg1, entry_size);
    __ bind(entry);
    __ bne(c_rarg1, c_rarg2, loop);
  }

  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                   InterpreterRuntime::throw_illegal_monitor_state_exception));
  __ should_not_reach_here();

  __ bind(found);
  __ push_ptr(x10);
  __ unlock_object(c_rarg1);
  __ pop_ptr(x10);
}

void TemplateTable::wide() {
  __ lbu(x9, at_bcp(1));
  __ mv(t0, (address)Interpreter::_wentry_point);
  __ shift_left_add(t1, x9, t0, 3);
  __ ld(t1, Address(t1, 0));
  __ jr(t1);
}

void TemplateTable::multianewarray() {
  transition(vtos, atos);
  __ lbu(x10, at_bcp(3));
  __ shift_left_add(c_rarg1, x10, esp, 3);
  __ subi(c_rarg1, c_rarg1, wordSize);
  call_VM(x10,
          CAST_FROM_FN_PTR(address, InterpreterRuntime::multianewarray),
          c_rarg1);
  __ lbu(x11, at_bcp(3));
  __ shift_left_add(esp, x11, esp, 3, t0);
}

#endif // !CC_INTERP
