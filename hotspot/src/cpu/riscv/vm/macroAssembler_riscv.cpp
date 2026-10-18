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
#include "runtime/thread.hpp"

// -------------------------------------------------------------
// 64-bit Pointer and Constant Loading (movptr)
// -------------------------------------------------------------
void MacroAssembler::movptr(Register dst, intptr_t val) {
  if (val == 0) {
    mv(dst, zr);
    return;
  }
  if (is_simm12(val)) {
    addi(dst, zr, (int32_t)val);
    return;
  }
  if (is_simm32(val)) {
    int32_t lo12 = ((int32_t)val << 20) >> 20;
    int32_t hi20 = (((int32_t)val - lo12) >> 12) & 0xfffff;
    lui(dst, hi20 << 12);
    if (lo12 != 0) {
      addiw(dst, dst, lo12);
    }
    return;
  }

  // General 64-bit decomposition: upper 32 bits and lower 32 bits
  int64_t upper = (int64_t)val >> 32;
  int32_t lower = (int32_t)val;

  movptr(dst, upper);
  slli(dst, dst, 32);

  if (lower != 0) {
    if (is_simm12(lower)) {
      addi(dst, dst, lower);
    } else {
      Register tmp = t0;
      int32_t lo12 = (lower << 20) >> 20;
      int32_t hi20 = ((lower - lo12) >> 12) & 0xfffff;
      lui(tmp, hi20 << 12);
      if (lo12 != 0) {
        addiw(tmp, tmp, lo12);
      }
      slli(tmp, tmp, 32);
      srli(tmp, tmp, 32);
      orr(dst, dst, tmp);
    }
  }
}

// -------------------------------------------------------------
// Memory Operations using Address
// -------------------------------------------------------------
void MacroAssembler::ld(Register dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::ld(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::ld(dst, tmp, 0);
  }
}

void MacroAssembler::sd(Register src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::sd(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sd(src, tmp, 0);
  }
}

void MacroAssembler::lw(Register dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lw(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lw(dst, tmp, 0);
  }
}

void MacroAssembler::sw(Register src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::sw(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sw(src, tmp, 0);
  }
}

void MacroAssembler::lh(Register dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lh(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lh(dst, tmp, 0);
  }
}

void MacroAssembler::sh(Register src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::sh(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sh(src, tmp, 0);
  }
}

void MacroAssembler::lb(Register dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lb(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lb(dst, tmp, 0);
  }
}

void MacroAssembler::sb(Register src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::sb(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sb(src, tmp, 0);
  }
}

void MacroAssembler::lbu(Register rd, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lbu(rd, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lbu(rd, tmp, 0);
  }
}

void MacroAssembler::lhu(Register rd, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lhu(rd, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lhu(rd, tmp, 0);
  }
}

void MacroAssembler::lwu(Register rd, Address adr) {
  if (adr.is_simm12()) {
    Assembler::lwu(rd, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::lwu(rd, tmp, 0);
  }
}

void MacroAssembler::fld(FloatRegister dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::fld(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::fld(dst, tmp, 0);
  }
}

void MacroAssembler::fsd(FloatRegister src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::fsd(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::fsd(src, tmp, 0);
  }
}

void MacroAssembler::flw(FloatRegister dst, Address adr) {
  if (adr.is_simm12()) {
    Assembler::flw(dst, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::flw(dst, tmp, 0);
  }
}

void MacroAssembler::fsw(FloatRegister src, Address adr) {
  if (adr.is_simm12()) {
    Assembler::fsw(src, adr.base(), adr.offset());
  } else {
    Register tmp = t0;
    movptr(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::fsw(src, tmp, 0);
  }
}

// -------------------------------------------------------------
// Stack Frame Construction
// -------------------------------------------------------------
void MacroAssembler::build_frame(int framesize) {
  assert(framesize >= 16 && (framesize % 16) == 0, "frame size must be 16-byte aligned");
  addi(sp, sp, -framesize);
  sd(ra, Address(sp, framesize - 8));
  sd(fp, Address(sp, framesize - 16));
  addi(fp, sp, framesize);
}

void MacroAssembler::remove_frame(int framesize) {
  assert(framesize >= 16 && (framesize % 16) == 0, "frame size must be 16-byte aligned");
  ld(ra, Address(sp, framesize - 8));
  ld(fp, Address(sp, framesize - 16));
  addi(sp, sp, framesize);
}

void MacroAssembler::bang_stack_with_offset(int offset) {
  sd(zr, Address(sp, -offset));
}

// -------------------------------------------------------------
// Calls and Branches
// -------------------------------------------------------------
void MacroAssembler::call(address entry) {
  movptr(t0, (intptr_t)entry);
  jalr(ra, t0, 0);
}

void MacroAssembler::jmp(address entry) {
  movptr(t0, (intptr_t)entry);
  jalr(zr, t0, 0);
}

void MacroAssembler::safepoint_poll(Register tmp) {
  address poll_address = os::get_polling_page();
  movptr(tmp, (intptr_t)poll_address);
  ld(zr, Address(tmp, 0));
}

// -------------------------------------------------------------
// Java Frame Anchor
// -------------------------------------------------------------
void MacroAssembler::set_last_Java_frame(Register last_java_sp,
                                         Register last_java_fp,
                                         address  last_java_pc,
                                         Register tmp) {
  if (last_java_sp->is_valid()) {
    sd(last_java_sp, Address(xthread, JavaThread::last_Java_sp_offset()));
  }
  if (last_java_fp->is_valid()) {
    sd(last_java_fp, Address(xthread, JavaThread::last_Java_fp_offset()));
  }
  if (last_java_pc != NULL) {
    movptr(tmp, (intptr_t)last_java_pc);
    sd(tmp, Address(xthread, JavaThread::last_Java_pc_offset()));
  }
}

void MacroAssembler::reset_last_Java_frame(bool clear_fp) {
  sd(zr, Address(xthread, JavaThread::last_Java_sp_offset()));
  sd(zr, Address(xthread, JavaThread::last_Java_pc_offset()));
  if (clear_fp) {
    sd(zr, Address(xthread, JavaThread::last_Java_fp_offset()));
  }
}

// -------------------------------------------------------------
// Runtime Calls (call_VM)
// -------------------------------------------------------------
void MacroAssembler::call_VM_leaf(address entry_point, int number_of_arguments) {
  call(entry_point);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1) {
  mv(c_rarg0, arg_1);
  call(entry_point);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1, Register arg_2) {
  mv(c_rarg0, arg_1);
  mv(c_rarg1, arg_2);
  call(entry_point);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1, Register arg_2, Register arg_3) {
  mv(c_rarg0, arg_1);
  mv(c_rarg1, arg_2);
  mv(c_rarg2, arg_3);
  call(entry_point);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, bool check_exceptions) {
  mv(c_rarg0, xthread);
  call(entry_point);
  if (oop_result->is_valid()) {
    ld(oop_result, Address(xthread, JavaThread::vm_result_offset()));
    sd(zr, Address(xthread, JavaThread::vm_result_offset()));
  }
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, bool check_exceptions) {
  mv(c_rarg0, xthread);
  mv(c_rarg1, arg_1);
  call(entry_point);
  if (oop_result->is_valid()) {
    ld(oop_result, Address(xthread, JavaThread::vm_result_offset()));
    sd(zr, Address(xthread, JavaThread::vm_result_offset()));
  }
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, bool check_exceptions) {
  mv(c_rarg0, xthread);
  mv(c_rarg1, arg_1);
  mv(c_rarg2, arg_2);
  call(entry_point);
  if (oop_result->is_valid()) {
    ld(oop_result, Address(xthread, JavaThread::vm_result_offset()));
    sd(zr, Address(xthread, JavaThread::vm_result_offset()));
  }
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions) {
  mv(c_rarg0, xthread);
  mv(c_rarg1, arg_1);
  mv(c_rarg2, arg_2);
  mv(c_rarg3, arg_3);
  call(entry_point);
  if (oop_result->is_valid()) {
    ld(oop_result, Address(xthread, JavaThread::vm_result_offset()));
    sd(zr, Address(xthread, JavaThread::vm_result_offset()));
  }
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, int number_of_arguments, bool check_exceptions) {
  set_last_Java_frame(last_java_sp, fp, NULL, t0);
  call_VM(oop_result, entry_point, check_exceptions);
  reset_last_Java_frame(false);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, bool check_exceptions) {
  set_last_Java_frame(last_java_sp, fp, NULL, t0);
  call_VM(oop_result, entry_point, arg_1, check_exceptions);
  reset_last_Java_frame(false);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, bool check_exceptions) {
  set_last_Java_frame(last_java_sp, fp, NULL, t0);
  call_VM(oop_result, entry_point, arg_1, arg_2, check_exceptions);
  reset_last_Java_frame(false);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions) {
  set_last_Java_frame(last_java_sp, fp, NULL, t0);
  call_VM(oop_result, entry_point, arg_1, arg_2, arg_3, check_exceptions);
  reset_last_Java_frame(false);
}

// -------------------------------------------------------------
// Null Checks
// -------------------------------------------------------------
void MacroAssembler::null_check(Register reg, int offset) {
  if (needs_explicit_null_check(offset)) {
    // Explicit null check
    bne(reg, zr, 8); // Skip ebreak if reg != 0
    stop("NullPointerException");
  } else {
    // Implicit null check: touch address 0(reg)
    ld(zr, Address(reg, 0));
  }
}

bool MacroAssembler::uses_implicit_null_check(void* address) {
  return (uintptr_t)address < (uintptr_t)os::vm_page_size();
}
