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
#include "memory/barrierSet.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "memory/universe.hpp"
#include "oops/klass.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "memory/metaspace.hpp"
#include "oops/method.hpp"

// -------------------------------------------------------------
// 64-bit Pointer and Constant Loading (movptr)
// -------------------------------------------------------------
void MacroAssembler::movptr_with_offset(Register Rd, address addr, int32_t &offset) {
  int64_t imm64 = (int64_t)addr;
  // Load upper 31 bits
  int64_t imm = imm64 >> 17;
  int64_t upper = imm, lower = imm;
  lower = (lower << 52) >> 52;
  upper -= lower;
  upper = (int32_t)upper;
  lui(Rd, upper);
  addi(Rd, Rd, lower);

  // Load the rest 17 bits.
  slli(Rd, Rd, 11);
  addi(Rd, Rd, (imm64 >> 6) & 0x7ff);
  slli(Rd, Rd, 6);

  // This offset will be used by following jalr/ld.
  offset = imm64 & 0x3f;
}

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

  // 48-bit address/pointer without using any scratch register
  int32_t offset = 0;
  movptr_with_offset(dst, (address)val, offset);
  if (offset != 0) {
    addi(dst, dst, offset);
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

// Java expression stack operations (esp)
// Conforms to AArch64 port design and HotSpot interpreter conventions:
// push/pop on registers manipulate Java expression stack (esp).
// C stack (sp) operations require explicit 16-byte alignment under RISC-V ABI.
void MacroAssembler::push(Register r) {
  addi(esp, esp, -wordSize);
  sd(r, Address(esp, 0));
}

void MacroAssembler::pop(Register r) {
  ld(r, Address(esp, 0));
  addi(esp, esp, wordSize);
}

void MacroAssembler::push(FloatRegister r) {
  addi(esp, esp, -wordSize);
  fsd(r, Address(esp, 0));
}

void MacroAssembler::pop(FloatRegister r) {
  fld(r, Address(esp, 0));
  addi(esp, esp, wordSize);
}

void MacroAssembler::push_reg(Register r) {
  push(r);
}

void MacroAssembler::pop_reg(Register r) {
  pop(r);
}

void MacroAssembler::enter() {
  build_frame(16);
}

void MacroAssembler::leave() {
  mv(sp, fp);
  ld(ra, Address(sp, -8));
  ld(fp, Address(sp, -16));
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

void MacroAssembler::far_jump(const Address& entry, Register tmp) {
  if (entry.target() != NULL) {
    movptr(tmp, (intptr_t)entry.target());
    jalr(zr, tmp, 0);
  } else {
    movptr(tmp, entry.offset());
    add(tmp, entry.base(), tmp);
    jalr(zr, tmp, 0);
  }
}

void MacroAssembler::far_call(const Address& entry, Register tmp) {
  if (entry.target() != NULL) {
    movptr(tmp, (intptr_t)entry.target());
    jalr(ra, tmp, 0);
  } else {
    movptr(tmp, entry.offset());
    add(tmp, entry.base(), tmp);
    jalr(ra, tmp, 0);
  }
}

void MacroAssembler::la(Register Rd, const Address& adr) {
  if (adr.getMode() == Address::literal) {
    movptr(Rd, (intptr_t)adr.target());
  } else {
    if (adr.is_simm12()) {
      addi(Rd, adr.base(), adr.offset());
    } else {
      movptr(Rd, adr.offset());
      add(Rd, adr.base(), Rd);
    }
  }
}

void MacroAssembler::la(Register Rd, const address addr) {
  movptr(Rd, (intptr_t)addr);
}

void MacroAssembler::la(Register Rd, Label& L) {
  if (L.is_bound()) {
    int64_t distance = target(L) - pc();
    auipc(Rd, (int32_t)distance + 0x800);
    int32_t offset = ((int32_t)distance << 20) >> 20;
    addi(Rd, Rd, offset);
  } else {
    L.add_patch_at(code(), locator());
    auipc(Rd, 0);
    addi(Rd, Rd, 0);
  }
}

// -------------------------------------------------------------
// Branch and Jump with Labels
// -------------------------------------------------------------
void MacroAssembler::beq(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::beq(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::beq(rs1, rs2, 0);
  }
}

void MacroAssembler::bne(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bne(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bne(rs1, rs2, 0);
  }
}

void MacroAssembler::blt(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::blt(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::blt(rs1, rs2, 0);
  }
}

void MacroAssembler::bge(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bge(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bge(rs1, rs2, 0);
  }
}

void MacroAssembler::bltu(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bltu(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bltu(rs1, rs2, 0);
  }
}

void MacroAssembler::bgeu(Register rs1, Register rs2, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bgeu(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bgeu(rs1, rs2, 0);
  }
}

void MacroAssembler::j(Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::jal(zr, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::jal(zr, 0);
  }
}

void MacroAssembler::jal(Register rd, Label& L) {
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::jal(rd, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::jal(rd, 0);
  }
}

void MacroAssembler::pd_patch_instruction(address branch, address target) {
  uint32_t insn = *(uint32_t*)branch;
  int32_t offset = (int32_t)(target - branch);
  uint32_t opcode = insn & 0x7f;
  if (opcode == 0x63) { // BRANCH (B-format)
    assert(is_simm13(offset) && (offset & 1) == 0, "branch offset out of range");
    uint32_t imm12 = (offset >> 12) & 0x1;
    uint32_t imm11 = (offset >> 11) & 0x1;
    uint32_t imm10_5 = (offset >> 5) & 0x3f;
    uint32_t imm4_1 = (offset >> 1) & 0xf;
    insn &= ~((1U << 31) | (0x3f << 25) | (0xf << 8) | (1 << 7));
    insn |= (imm12 << 31) | (imm10_5 << 25) | (imm4_1 << 8) | (imm11 << 7);
    *(uint32_t*)branch = insn;
  } else if (opcode == 0x6f) { // JAL (J-format)
    assert(is_simm21(offset) && (offset & 1) == 0, "jal offset out of range");
    uint32_t imm20 = (offset >> 20) & 0x1;
    uint32_t imm19_12 = (offset >> 12) & 0xff;
    uint32_t imm11 = (offset >> 11) & 0x1;
    uint32_t imm10_1 = (offset >> 1) & 0x3ff;
    insn &= 0xfff;
    insn |= (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | (imm19_12 << 12);
    *(uint32_t*)branch = insn;
  } else if (opcode == 0x17) { // AUIPC (PC-relative pair: auipc + addi)
    uint32_t insn2 = *(uint32_t*)(branch + 4);
    assert((insn2 & 0x7f) == 0x13, "expected addi following auipc");
    insn &= 0xfff;
    insn |= (((offset + 0x800) >> 12) & 0xfffff) << 12;
    *(uint32_t*)branch = insn;

    insn2 &= 0x000fffff;
    insn2 |= ((uint32_t)(offset & 0xfff)) << 20;
    *(uint32_t*)(branch + 4) = insn2;
  } else {
    guarantee(false, "unsupported branch instruction to patch");
  }
}

void MacroAssembler::reinit_heapbase() {
  if (UseCompressedOops || UseCompressedClassPointers) {
    if (Universe::is_fully_initialized()) {
      movptr(xheapbase, (intptr_t)Universe::narrow_oop_base());
    } else {
      movptr(xheapbase, 0);
    }
  }
}

void MacroAssembler::load_byte_map_base(Register dst) {
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->kind() == BarrierSet::CardTableModRef ||
         bs->kind() == BarrierSet::CardTableExtension, "wrong barrier set kind");
  CardTableModRefBS* ct = (CardTableModRefBS*)bs;
  assert(sizeof(*ct->byte_map_base) == sizeof(jbyte), "adjust this code");
  movptr(dst, (intptr_t)ct->byte_map_base);
}

void MacroAssembler::test_bit(Register Rd, Register Rs, uint32_t bit_pos) {
  assert(bit_pos < 64, "invalid bit range");
  int64_t imm = (int64_t)(1UL << bit_pos);
  if (is_simm12(imm)) {
    andi(Rd, Rs, imm);
  } else {
    srli(Rd, Rs, bit_pos);
    andi(Rd, Rd, 1);
  }
}

void MacroAssembler::safepoint_poll(Register tmp) {
  address poll_address = os::get_polling_page();
  movptr(tmp, (intptr_t)poll_address);
  ld(zr, Address(tmp, 0));
}

void MacroAssembler::shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt) {
  shift_left_add(Rd, Rs1, Rs2, shamt, Rd);
}

void MacroAssembler::shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt, Register tmp) {
  assert_different_registers(Rs2, tmp);
  if (shamt != 0) {
    slli(tmp, Rs1, shamt);
    add(Rd, Rs2, tmp);
  } else {
    add(Rd, Rs1, Rs2);
  }
}

void MacroAssembler::membar(uint32_t order_constraint) {
  uint32_t predecessor = (order_constraint >> 2) & 0x3;
  uint32_t successor = order_constraint & 0x3;
  fence(predecessor, successor);
}

void MacroAssembler::zext(Register dst, Register src, int bits) {
  if (bits == 64) {
    if (dst != src) mv(dst, src);
    return;
  }
  slli(dst, src, 64 - bits);
  srli(dst, dst, 64 - bits);
}

void MacroAssembler::sext(Register dst, Register src, int bits) {
  if (bits == 64) {
    if (dst != src) mv(dst, src);
    return;
  }
  if (bits == 32) {
    addiw(dst, src, 0);
    return;
  }
  slli(dst, src, 64 - bits);
  srai(dst, dst, 64 - bits);
}

void MacroAssembler::cast_primitive_type(BasicType type, Register Rt) {
  switch (type) {
    case T_BOOLEAN:
      c2bool(Rt);
      break;
    case T_CHAR:
      zext(Rt, Rt, 16);
      break;
    case T_BYTE:
      sext(Rt, Rt, 8);
      break;
    case T_SHORT:
      sext(Rt, Rt, 16);
      break;
    case T_INT:
      sext(Rt, Rt, 32);
      break;
    case T_LONG:
    case T_VOID:
    case T_FLOAT:
    case T_DOUBLE:
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::encode_heap_oop(Register d, Register s) {
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      srli(d, s, LogMinObjAlignmentInBytes);
    } else {
      if (d != s) mv(d, s);
    }
  } else {
    Label notNull;
    sub(d, s, xheapbase);
    bgez(d, notNull);
    mv(d, zr);
    bind(notNull);
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      srli(d, d, LogMinObjAlignmentInBytes);
    }
  }
}

void MacroAssembler::decode_heap_oop(Register d, Register s) {
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      slli(d, s, LogMinObjAlignmentInBytes);
    } else {
      if (d != s) mv(d, s);
    }
  } else {
    Label done;
    if (d != s) mv(d, s);
    beqz(d, done);
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shift_left_add(d, d, xheapbase, LogMinObjAlignmentInBytes);
    } else {
      add(d, d, xheapbase);
    }
    bind(done);
  }
}

void MacroAssembler::encode_heap_oop_not_null(Register d, Register s) {
  if (Universe::narrow_oop_base() != NULL) {
    sub(d, s, xheapbase);
  } else {
    if (d != s) mv(d, s);
  }
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    srli(d, d, LogMinObjAlignmentInBytes);
  }
}

void MacroAssembler::decode_heap_oop_not_null(Register d, Register s) {
  if (Universe::narrow_oop_base() != NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shift_left_add(d, s, xheapbase, LogMinObjAlignmentInBytes);
    } else {
      add(d, s, xheapbase);
    }
  } else {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      slli(d, s, LogMinObjAlignmentInBytes);
    } else {
      if (d != s) mv(d, s);
    }
  }
}

void MacroAssembler::load_heap_oop(Register dst, Address src) {
  if (UseCompressedOops) {
    lwu(dst, src);
    decode_heap_oop(dst);
  } else {
    ld(dst, src);
  }
}

void MacroAssembler::load_heap_oop_not_null(Register dst, Address src) {
  if (UseCompressedOops) {
    lwu(dst, src);
    decode_heap_oop_not_null(dst);
  } else {
    ld(dst, src);
  }
}

void MacroAssembler::store_heap_oop(Address dst, Register src) {
  if (UseCompressedOops) {
    encode_heap_oop(src);
    sw(src, dst);
  } else {
    sd(src, dst);
  }
}

void MacroAssembler::store_heap_oop_null(Address dst) {
  if (UseCompressedOops) {
    sw(zr, dst);
  } else {
    sd(zr, dst);
  }
}

void MacroAssembler::store_check(Register obj) {
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->kind() == BarrierSet::CardTableModRef ||
         bs->kind() == BarrierSet::CardTableExtension, "wrong barrier set kind");
  srli(obj, obj, CardTableModRefBS::card_shift);
  load_byte_map_base(t1);
  add(t1, t1, obj);
  if (UseConcMarkSweepGC && CMSPrecleaningEnabled) {
    membar(StoreStore);
  }
  sb(zr, Address(t1, 0));
}

#if INCLUDE_ALL_GCS
void MacroAssembler::g1_write_barrier_pre(Register obj,
                                          Register pre_val,
                                          Register thread,
                                          Register tmp,
                                          bool tosca_live,
                                          bool expand_call) {
  Label done;
  Label runtime;

  assert_different_registers(obj, pre_val, tmp, t0);
  assert(pre_val != noreg && tmp != noreg, "expecting registers");

  Address in_progress(thread, in_bytes(JavaThread::satb_mark_queue_offset() +
                                       PtrQueue::byte_offset_of_active()));
  Address index(thread, in_bytes(JavaThread::satb_mark_queue_offset() +
                                 PtrQueue::byte_offset_of_index()));
  Address buffer(thread, in_bytes(JavaThread::satb_mark_queue_offset() +
                                  PtrQueue::byte_offset_of_buf()));

  // Is marking active?
  if (in_bytes(PtrQueue::byte_width_of_active()) == 4) {
    lwu(tmp, in_progress);
  } else {
    assert(in_bytes(PtrQueue::byte_width_of_active()) == 1, "Assumption");
    lbu(tmp, in_progress);
  }
  beqz(tmp, done);

  if (obj != noreg) {
    load_heap_oop(pre_val, Address(obj, 0));
  }
  beqz(pre_val, done);

  // Can we store into SATB queue buffer?
  ld(tmp, index);
  beqz(tmp, runtime);
  subi(tmp, tmp, wordSize);
  sd(tmp, index);
  ld(t0, buffer);
  add(t0, t0, tmp);
  sd(pre_val, Address(t0, 0));
  j(done);

  bind(runtime);
  if (tosca_live) {
    // Preserve x10 across runtime leaf call on C stack (sp) with 16-byte alignment
    addi(sp, sp, -16);
    sd(x10, Address(sp, 0));
  }
  call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::g1_wb_pre), pre_val, thread);
  if (tosca_live) {
    ld(x10, Address(sp, 0));
    addi(sp, sp, 16);
  }

  bind(done);
}

void MacroAssembler::g1_write_barrier_post(Register store_addr,
                                           Register new_val,
                                           Register thread,
                                           Register tmp,
                                           Register tmp2) {
  Label done;
  Label runtime;

  assert_different_registers(store_addr, new_val, thread, tmp, tmp2);

  // Cross-region check
  xorr(tmp, store_addr, new_val);
  srli(tmp, tmp, HeapRegion::LogOfHRGrainBytes);
  beqz(tmp, done);

  // Filter card
  srli(tmp, store_addr, CardTableModRefBS::card_shift);
  load_byte_map_base(tmp2);
  add(tmp2, tmp2, tmp);

  lbu(tmp, Address(tmp2, 0));
  addi(tmp, tmp, - (int)G1SATBCardTableModRefBS::g1_young_card_val());
  beqz(tmp, done);

  membar(StoreLoad);

  lbu(tmp, Address(tmp2, 0));
  addi(tmp, tmp, - (int)CardTableModRefBS::dirty_card_val());
  beqz(tmp, done);

  sb(zr, Address(tmp2, 0));

  // Enqueue card to dirty card queue
  Address card_index(thread, in_bytes(JavaThread::dirty_card_queue_offset() +
                                      PtrQueue::byte_offset_of_index()));
  Address card_buffer(thread, in_bytes(JavaThread::dirty_card_queue_offset() +
                                       PtrQueue::byte_offset_of_buf()));

  ld(tmp, card_index);
  beqz(tmp, runtime);
  subi(tmp, tmp, wordSize);
  sd(tmp, card_index);
  ld(t0, card_buffer);
  add(t0, t0, tmp);
  sd(tmp2, Address(t0, 0));
  j(done);

  bind(runtime);
  call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::g1_wb_post), tmp2, thread);

  bind(done);
}
#endif

// -------------------------------------------------------------
// Java Frame Anchor
// -------------------------------------------------------------
void MacroAssembler::set_last_Java_frame(Register last_java_sp,
                                         Register last_java_fp,
                                         address  last_java_pc,
                                         Register tmp) {
  if (last_java_pc != NULL) {
    movptr(tmp, (intptr_t)last_java_pc);
    sd(tmp, Address(xthread, JavaThread::last_Java_pc_offset()));
  }
  if (last_java_fp->is_valid()) {
    sd(last_java_fp, Address(xthread, JavaThread::last_Java_fp_offset()));
  }
  if (last_java_sp->is_valid()) {
    sd(last_java_sp, Address(xthread, JavaThread::last_Java_sp_offset()));
  }
}

void MacroAssembler::set_last_Java_frame(Register last_java_sp,
                                         Register last_java_fp,
                                         Label& L,
                                         Register tmp) {
  la(tmp, L);
  sd(tmp, Address(xthread, JavaThread::last_Java_pc_offset()));
  if (last_java_fp->is_valid()) {
    sd(last_java_fp, Address(xthread, JavaThread::last_Java_fp_offset()));
  }
  if (last_java_sp->is_valid()) {
    sd(last_java_sp, Address(xthread, JavaThread::last_Java_sp_offset()));
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
void MacroAssembler::call_VM_leaf_base(address entry_point,
                                       int number_of_arguments,
                                       Label *retaddr) {
  // Push t0 and xmethod (x31/t6) to sp.
  // In RISC-V psABI, t6 and t0 are caller-saved and will be clobbered by C/C++ VM calls.
  // Pushing 2 registers (16 bytes) also maintains 16-byte stack alignment.
  subi(sp, sp, 2 * wordSize);
  sd(t0, Address(sp, 0));
  sd(xmethod, Address(sp, wordSize));

  call(entry_point);

  if (retaddr != NULL) {
    bind(*retaddr);
  }

  ld(t0, Address(sp, 0));
  ld(xmethod, Address(sp, wordSize));
  addi(sp, sp, 2 * wordSize);
}

void MacroAssembler::call_VM_leaf(address entry_point, int number_of_arguments) {
  call_VM_leaf_base(entry_point, number_of_arguments);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1) {
  mv(c_rarg0, arg_1);
  call_VM_leaf_base(entry_point, 1);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1, Register arg_2) {
  mv(c_rarg0, arg_1);
  mv(c_rarg1, arg_2);
  call_VM_leaf_base(entry_point, 2);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_1, Register arg_2, Register arg_3) {
  mv(c_rarg0, arg_1);
  mv(c_rarg1, arg_2);
  mv(c_rarg2, arg_3);
  call_VM_leaf_base(entry_point, 3);
}

void MacroAssembler::call_VM_base(Register oop_result,
                                  Register java_thread,
                                  Register last_java_sp,
                                  address  entry_point,
                                  int      number_of_arguments,
                                  bool     check_exceptions) {
  if (!java_thread->is_valid()) {
    java_thread = xthread;
  }
  if (!last_java_sp->is_valid()) {
    last_java_sp = esp;
  }

  assert(number_of_arguments >= 0, "cannot have negative number of arguments");
  assert(java_thread == xthread, "unexpected register");
  assert(java_thread != oop_result, "cannot use the same register for java_thread & oop_result");
  assert(java_thread != last_java_sp, "cannot use the same register for java_thread & last_java_sp");

  // push java thread (becomes first argument of C function)
  mv(c_rarg0, java_thread);

  // set last Java frame before call
  assert(last_java_sp != fp, "can't use fp");

  Label l;
  set_last_Java_frame(last_java_sp, fp, l, t0);

  // call VM entry point
  call_VM_leaf_base(entry_point, number_of_arguments, &l);

  // reset last Java frame
  reset_last_Java_frame(true);

  check_and_handle_popframe(java_thread);
  check_and_handle_earlyret(java_thread);

  if (check_exceptions) {
    ld(t0, Address(java_thread, in_bytes(Thread::pending_exception_offset())));
    Label ok;
    beqz(t0, ok);
    far_jump(RuntimeAddress(StubRoutines::forward_exception_entry()));
    bind(ok);
  }

  if (oop_result->is_valid()) {
    get_vm_result(oop_result, java_thread);
  }
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, bool check_exceptions) {
  call_VM(oop_result, noreg, entry_point, 0, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, bool check_exceptions) {
  mv(c_rarg1, arg_1);
  call_VM(oop_result, noreg, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, bool check_exceptions) {
  assert(arg_1 != c_rarg2, "smashed arg");
  mv(c_rarg2, arg_2);
  mv(c_rarg1, arg_1);
  call_VM(oop_result, noreg, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions) {
  assert(arg_1 != c_rarg3 && arg_2 != c_rarg3, "smashed arg");
  mv(c_rarg3, arg_3);
  assert(arg_1 != c_rarg2, "smashed arg");
  mv(c_rarg2, arg_2);
  mv(c_rarg1, arg_1);
  call_VM(oop_result, noreg, entry_point, 3, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, int number_of_arguments, bool check_exceptions) {
  call_VM_base(oop_result, xthread, last_java_sp, entry_point, number_of_arguments, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, bool check_exceptions) {
  mv(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, bool check_exceptions) {
  assert(arg_1 != c_rarg2, "smashed arg");
  mv(c_rarg2, arg_2);
  mv(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions) {
  assert(arg_1 != c_rarg3 && arg_2 != c_rarg3, "smashed arg");
  mv(c_rarg3, arg_3);
  assert(arg_1 != c_rarg2, "smashed arg");
  mv(c_rarg2, arg_2);
  mv(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 3, check_exceptions);
}

void MacroAssembler::get_vm_result(Register oop_result, Register java_thread) {
  ld(oop_result, Address(java_thread, JavaThread::vm_result_offset()));
  sd(zr, Address(java_thread, JavaThread::vm_result_offset()));
  verify_oop(oop_result, "broken oop in call_VM_base");
}

void MacroAssembler::get_vm_result_2(Register metadata_result, Register java_thread) {
  ld(metadata_result, Address(java_thread, JavaThread::vm_result_2_offset()));
  sd(zr, Address(java_thread, JavaThread::vm_result_2_offset()));
}

void MacroAssembler::load_sized_value(Register dst, Address src, size_t size_in_bytes, bool is_signed) {
  switch (size_in_bytes) {
    case 1:  is_signed ? lb(dst, src) : lbu(dst, src); break;
    case 2:  is_signed ? lh(dst, src) : lhu(dst, src); break;
    case 4:  is_signed ? lw(dst, src) : lwu(dst, src); break;
    case 8:  ld(dst, src); break;
    default: ShouldNotReachHere();
  }
}

// -------------------------------------------------------------
// Null Checks
// -------------------------------------------------------------
void MacroAssembler::null_check(Register reg, int offset) {
  if (needs_explicit_null_check(offset)) {
    // Explicit null check
    Label L;
    bne(reg, zr, L);
    stop("NullPointerException");
    bind(L);
  } else {
    // Implicit null check: touch address 0(reg)
    ld(zr, Address(reg, 0));
  }
}

bool MacroAssembler::uses_implicit_null_check(void* address) {
  return (uintptr_t)address < (uintptr_t)os::vm_page_size();
}

// -------------------------------------------------------------
// Klass Encoding/Decoding and Loading/Storing
// -------------------------------------------------------------
void MacroAssembler::encode_klass_not_null(Register r, Register tmp) {
  encode_klass_not_null(r, r, tmp);
}

void MacroAssembler::encode_klass_not_null(Register dst, Register src, Register tmp) {
  if (Universe::narrow_klass_base() == NULL) {
    if (Universe::narrow_klass_shift() != 0) {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      srli(dst, src, LogKlassAlignmentInBytes);
    } else {
      if (dst != src) mv(dst, src);
    }
    return;
  }

  Register rbase = dst;
  if (dst == src) rbase = tmp;
  movptr(rbase, (intptr_t)Universe::narrow_klass_base());
  sub(dst, src, rbase);
  if (Universe::narrow_klass_shift() != 0) {
    assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
    srli(dst, dst, LogKlassAlignmentInBytes);
  }
}

void MacroAssembler::decode_klass_not_null(Register r, Register tmp) {
  decode_klass_not_null(r, r, tmp);
}

void MacroAssembler::decode_klass_not_null(Register dst, Register src, Register tmp) {
  if (Universe::narrow_klass_base() != NULL) {
    Register rbase = dst;
    if (dst == src) rbase = tmp;
    movptr(rbase, (intptr_t)Universe::narrow_klass_base());
    if (Universe::narrow_klass_shift() != 0) {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      shift_left_add(dst, src, rbase, LogKlassAlignmentInBytes);
    } else {
      add(dst, src, rbase);
    }
  } else {
    if (Universe::narrow_klass_shift() != 0) {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      slli(dst, src, LogKlassAlignmentInBytes);
    } else {
      if (dst != src) mv(dst, src);
    }
  }
}

void MacroAssembler::load_klass(Register dst, Register src) {
  if (UseCompressedClassPointers) {
    lwu(dst, Address(src, oopDesc::klass_offset_in_bytes()));
    decode_klass_not_null(dst);
  } else {
    ld(dst, Address(src, oopDesc::klass_offset_in_bytes()));
  }
}

void MacroAssembler::store_klass(Register dst, Register src) {
  if (UseCompressedClassPointers) {
    encode_klass_not_null(src);
    sw(src, Address(dst, oopDesc::klass_offset_in_bytes()));
  } else {
    sd(src, Address(dst, oopDesc::klass_offset_in_bytes()));
  }
}

void MacroAssembler::store_klass_gap(Register dst, Register src) {
  if (UseCompressedClassPointers) {
    sw(src, Address(dst, oopDesc::klass_gap_offset_in_bytes()));
  }
}

// -------------------------------------------------------------
// Subtype Checking
// -------------------------------------------------------------
void MacroAssembler::check_klass_subtype(Register sub_klass,
                                         Register super_klass,
                                         Register temp_reg,
                                         Label& L_success) {
  Label L_failure;
  check_klass_subtype_fast_path(sub_klass, super_klass, temp_reg, &L_success, &L_failure, NULL);
  check_klass_subtype_slow_path_linear(sub_klass, super_klass, temp_reg, noreg, &L_success, NULL);
  bind(L_failure);
}

void MacroAssembler::check_klass_subtype_fast_path(Register sub_klass,
                                                   Register super_klass,
                                                   Register temp_reg,
                                                   Label* L_success,
                                                   Label* L_failure,
                                                   Label* L_slow_path) {
  assert_different_registers(sub_klass, super_klass, temp_reg);
  Label L_fallthrough;
  if (L_success == NULL) L_success = &L_fallthrough;
  if (L_failure == NULL) L_failure = &L_fallthrough;

  int sco_offset = in_bytes(Klass::super_check_offset_offset());
  int sc_offset  = in_bytes(Klass::secondary_super_cache_offset());

  // 1. If same klass, success
  beq(sub_klass, super_klass, *L_success);

  // 2. Load displayed supertype
  lwu(temp_reg, Address(super_klass, sco_offset));
  add(t0, sub_klass, temp_reg);
  ld(t0, Address(t0, 0));
  beq(super_klass, t0, *L_success);

  // 3. If primary super, fail immediately
  mv(t1, sc_offset);
  if (L_slow_path == NULL) {
    bne(temp_reg, t1, *L_failure);
  } else {
    beq(temp_reg, t1, *L_slow_path);
    if (L_failure != &L_fallthrough) {
      j(*L_failure);
    }
  }

  bind(L_fallthrough);
}

void MacroAssembler::check_klass_subtype_slow_path_linear(Register sub_klass,
                                                          Register super_klass,
                                                          Register tmp1_reg,
                                                          Register tmp2_reg,
                                                          Label* L_success,
                                                          Label* L_failure) {
  Label L_fallthrough;
  if (L_success == NULL) L_success = &L_fallthrough;
  if (L_failure == NULL) L_failure = &L_fallthrough;

  int ss_offset = in_bytes(Klass::secondary_supers_offset());
  int sc_offset = in_bytes(Klass::secondary_super_cache_offset());

  // Scan secondary supers array
  ld(t1, Address(sub_klass, ss_offset));
  lwu(t2, Address(t1, Array<Klass*>::length_offset_in_bytes()));
  addi(t1, t1, Array<Klass*>::base_offset_in_bytes());

  Label loop, exit;
  beqz(t2, exit);
  bind(loop);
  ld(t0, Address(t1, 0));
  beq(super_klass, t0, exit);
  addi(t1, t1, wordSize);
  subi(t2, t2, 1);
  bnez(t2, loop);
  bind(exit);

  bne(super_klass, t0, *L_failure);

  // Success: update secondary_super_cache
  sd(super_klass, Address(sub_klass, sc_offset));
  if (L_success != &L_fallthrough) {
    j(*L_success);
  }
  bind(L_fallthrough);
}

// -------------------------------------------------------------
// Method Lookup
// -------------------------------------------------------------
void MacroAssembler::lookup_virtual_method(Register recv_klass,
                                           RegisterOrConstant vtable_index,
                                           Register method_result) {
  const int base = InstanceKlass::vtable_start_offset() * wordSize;
  assert(vtableEntry::size() * wordSize == 8, "adjust the scaling in the code below");
  int vtable_offset_in_bytes = base + vtableEntry::method_offset_in_bytes();

  if (vtable_index.is_register()) {
    shift_left_add(method_result, vtable_index.as_register(), recv_klass, LogBytesPerWord);
    ld(method_result, Address(method_result, vtable_offset_in_bytes));
  } else {
    vtable_offset_in_bytes += vtable_index.as_constant() * wordSize;
    if (is_simm12(vtable_offset_in_bytes)) {
      ld(method_result, Address(recv_klass, vtable_offset_in_bytes));
    } else {
      movptr(method_result, vtable_offset_in_bytes);
      add(method_result, recv_klass, method_result);
      ld(method_result, Address(method_result, 0));
    }
  }
}

void MacroAssembler::lookup_interface_method(Register recv_klass,
                                             Register intf_klass,
                                             RegisterOrConstant itable_index,
                                             Register method_result,
                                             Register scan_temp,
                                             Label& L_no_such_interface,
                                             bool return_method) {
  assert_different_registers(recv_klass, intf_klass, scan_temp);
  assert_different_registers(method_result, intf_klass, scan_temp);

  int vtable_base = InstanceKlass::vtable_start_offset() * wordSize;
  int itentry_off = itableMethodEntry::method_offset_in_bytes();
  int scan_step   = itableOffsetEntry::size() * wordSize;

  lwu(scan_temp, Address(recv_klass, InstanceKlass::vtable_length_offset() * wordSize));
  slli(scan_temp, scan_temp, 3);
  add(scan_temp, scan_temp, recv_klass);
  addi(scan_temp, scan_temp, vtable_base);
  round_to(scan_temp, BytesPerLong);

  if (return_method) {
    if (itable_index.is_register()) {
      slli(t0, itable_index.as_register(), 3);
      add(recv_klass, recv_klass, t0);
    } else {
      addi(recv_klass, recv_klass, itable_index.as_constant() * wordSize);
    }
    if (itentry_off) {
      addi(recv_klass, recv_klass, itentry_off);
    }
  }

  Label search, found_method;
  bind(search);
  ld(method_result, Address(scan_temp, itableOffsetEntry::interface_offset_in_bytes()));
  beq(intf_klass, method_result, found_method);
  beqz(method_result, L_no_such_interface);
  addi(scan_temp, scan_temp, scan_step);
  j(search);

  bind(found_method);
  if (return_method) {
    lwu(scan_temp, Address(scan_temp, itableOffsetEntry::offset_offset_in_bytes()));
    add(scan_temp, recv_klass, scan_temp);
    ld(method_result, Address(scan_temp, 0));
  }
}

// -------------------------------------------------------------
// Allocation Helpers
// -------------------------------------------------------------
void MacroAssembler::tlab_allocate(Register obj,
                                   Register var_size_in_bytes,
                                   int con_size_in_bytes,
                                   Register t1,
                                   Register t2,
                                   Label& slow_case) {
  assert_different_registers(obj, t2);
  assert_different_registers(obj, var_size_in_bytes);
  Register end = t2;

  ld(obj, Address(xthread, JavaThread::tlab_top_offset()));
  if (var_size_in_bytes == noreg) {
    addi(end, obj, con_size_in_bytes);
  } else {
    add(end, obj, var_size_in_bytes);
  }
  ld(t0, Address(xthread, JavaThread::tlab_end_offset()));
  bgtu(end, t0, slow_case);

  // update the tlab top pointer
  sd(end, Address(xthread, JavaThread::tlab_top_offset()));

  if (var_size_in_bytes == end) {
    sub(var_size_in_bytes, var_size_in_bytes, obj);
  }
}

// -------------------------------------------------------------
// Comparisons
// -------------------------------------------------------------
void MacroAssembler::cmp_x2i(Register dst, Register src1, Register src2,
                             Register tmp, bool is_signed) {
  if (src1 == src2) {
    mv(dst, zr);
    return;
  }
  Label done;
  Register left = src1;
  Register right = src2;
  if (dst == src1) {
    assert_different_registers(dst, src2, tmp);
    mv(tmp, src1);
    left = tmp;
  } else if (dst == src2) {
    assert_different_registers(dst, src1, tmp);
    mv(tmp, src2);
    right = tmp;
  }

  // installs 1 if gt else 0
  if (is_signed) {
    slt(dst, right, left);
  } else {
    sltu(dst, right, left);
  }
  bnez(dst, done);
  if (is_signed) {
    slt(dst, left, right);
  } else {
    sltu(dst, left, right);
  }
  // dst = -1 if lt; else if eq , dst = 0
  neg(dst, dst);
  bind(done);
}

void MacroAssembler::cmp_l2i(Register dst, Register src1, Register src2, Register tmp) {
  cmp_x2i(dst, src1, src2, tmp);
}

void MacroAssembler::float_compare(Register result, FloatRegister Rs1,
                                   FloatRegister Rs2, int unordered_result) {
  Label Ldone;
  if (unordered_result < 0) {
    flt_s(result, Rs2, Rs1);
    bgtz(result, Ldone);
    feq_s(result, Rs1, Rs2);
    subi(result, result, 1);
    bind(Ldone);
  } else {
    flt_s(result, Rs1, Rs2);
    bgtz(result, Ldone);
    feq_s(result, Rs1, Rs2);
    subi(result, result, 1);
    bind(Ldone);
    neg(result, result);
  }
}

void MacroAssembler::double_compare(Register result, FloatRegister Rs1,
                                    FloatRegister Rs2, int unordered_result) {
  Label Ldone;
  if (unordered_result < 0) {
    flt_d(result, Rs2, Rs1);
    bgtz(result, Ldone);
    feq_d(result, Rs1, Rs2);
    subi(result, result, 1);
    bind(Ldone);
  } else {
    flt_d(result, Rs1, Rs2);
    bgtz(result, Ldone);
    feq_d(result, Rs1, Rs2);
    subi(result, result, 1);
    bind(Ldone);
    neg(result, result);
  }
}

// -------------------------------------------------------------
// Extensions and Conversions
// -------------------------------------------------------------
void MacroAssembler::narrow_subword_type(Register reg, BasicType bt) {
  switch (bt) {
    case T_SHORT:
      sext(reg, reg, 16);
      break;
    case T_CHAR:
      zext(reg, reg, 16);
      break;
    case T_BYTE:
      sext(reg, reg, 8);
      break;
    case T_BOOLEAN:
      andi(reg, reg, 1);
      break;
    default:
      ShouldNotReachHere();
  }
}

#define FCVT_SAFE(FLOATCVT, FLOATSIG)                                                     \
void MacroAssembler::FLOATCVT##_safe(Register dst, FloatRegister src, Register tmp) {     \
  Label done;                                                                             \
  assert_different_registers(dst, tmp);                                                   \
  fclass_##FLOATSIG(tmp, src);                                                            \
  mv(dst, zr);                                                                            \
  andi(tmp, tmp, 0x300); /* 0x300 = quiet NaN | signalling NaN */                         \
  bnez(tmp, done);                                                                        \
  FLOATCVT(dst, src);                                                                     \
  bind(done);                                                                             \
}

FCVT_SAFE(fcvt_w_s, s);
FCVT_SAFE(fcvt_l_s, s);
FCVT_SAFE(fcvt_w_d, d);
FCVT_SAFE(fcvt_l_d, d);

#undef FCVT_SAFE

void MacroAssembler::revbw(Register Rd, Register Rs, Register tmp1, Register tmp2) {
  assert_different_registers(Rs, tmp1, tmp2);
  assert_different_registers(Rd, tmp1, tmp2);
  zext(tmp1, Rs, 8);
  slli(tmp1, tmp1, 8);
  for (int step = 8; step < 24; step += 8) {
    srli(tmp2, Rs, step);
    zext(tmp2, tmp2, 8);
    orr(tmp1, tmp1, tmp2);
    slli(tmp1, tmp1, 8);
  }
  srli(Rd, Rs, 24);
  zext(Rd, Rd, 8);
  orr(Rd, tmp1, Rd);
  sext(Rd, Rd, 32);
}

void MacroAssembler::revb(Register Rd, Register Rs, Register tmp1, Register tmp2) {
  assert_different_registers(Rs, tmp1, tmp2);
  assert_different_registers(Rd, tmp1, tmp2);
  zext(tmp1, Rs, 8);
  slli(tmp1, tmp1, 8);
  for (int step = 8; step < 56; step += 8) {
    srli(tmp2, Rs, step);
    zext(tmp2, tmp2, 8);
    orr(tmp1, tmp1, tmp2);
    slli(tmp1, tmp1, 8);
  }
  srli(Rd, Rs, 56);
  zext(Rd, Rd, 8);
  orr(Rd, tmp1, Rd);
}

void MacroAssembler::subw(Register Rd, Register Rn, int64_t decrement, Register tmp) {
  int64_t val = -decrement;
  if (-2048 <= val && val <= 2047) {
    addiw(Rd, Rn, val);
  } else {
    movptr(tmp, val);
    addw(Rd, Rn, tmp);
  }
}

#ifndef PRODUCT
void MacroAssembler::verify_oop(Register reg, const char* s) {
}

void MacroAssembler::verify_oop_addr(Address addr, const char* s) {
}
#endif

bool AbstractAssembler::pd_check_instruction_mark() {
  return false;
}

void MacroAssembler::mov_metadata(Register dst, Metadata* obj) {
  int oop_index;
  if (obj == NULL) {
    oop_index = oop_recorder()->allocate_metadata_index(obj);
  } else {
    oop_index = oop_recorder()->find_index(obj);
  }
  RelocationHolder rspec = metadata_Relocation::spec(oop_index);
  movptr(dst, (intptr_t)obj);
}

void MacroAssembler::movoop(Register dst, jobject obj) {
  int oop_index;
  if (obj == NULL) {
    oop_index = oop_recorder()->allocate_oop_index(obj);
  } else {
    oop_index = oop_recorder()->find_index(obj);
  }
  RelocationHolder rspec = oop_Relocation::spec(oop_index);
  movptr(dst, (intptr_t)obj);
}

#include "memory/resourceArea.hpp"

void MacroAssembler::debug64(char* msg, address bcp, Method* m, intptr_t dispatched_bc) {
  ResourceMark rm;
  ttyLocker ttyl;
  int bc = -1;
  if (bcp != NULL) {
    bc = (unsigned char)*bcp;
  }
  tty->print_cr("### MacroAssembler::stop: %s", msg ? msg : "unknown");
  tty->print_cr("    dispatched_bc = " INTPTR_FORMAT " (%s)",
                dispatched_bc,
                (dispatched_bc >= 0 && dispatched_bc < Bytecodes::number_of_codes) ? Bytecodes::name((Bytecodes::Code)dispatched_bc) : "unknown");
  tty->print_cr("    bcp = " INTPTR_FORMAT " (*bcp = %d %s)",
                p2i(bcp), bc,
                (bc >= 0 && bc < Bytecodes::number_of_codes) ? Bytecodes::name((Bytecodes::Code)bc) : "unknown");
  if (bcp != NULL) {
    tty->print("    bytes around bcp [-8..+15]: ");
    for (int i = -8; i < 16; i++) {
      tty->print("%02x ", (unsigned char)bcp[i]);
    }
    tty->cr();
  }
  if (m != NULL && Metaspace::contains(m)) {
    tty->print_cr("    method = %s", m->name_and_sig_as_C_string());
    tty->print_cr("    code_base = " INTPTR_FORMAT ", code_size = %d, bci = %d",
                  p2i(m->code_base()), m->code_size(), (int)(bcp - m->code_base()));
  }
  fatal(err_msg("MacroAssembler::stop: %s", msg ? msg : "unknown"));
}

void MacroAssembler::stop(const char* msg) {
  mv(c_rarg3, t0);
  movptr(c_rarg0, (intptr_t)msg);
  mv(c_rarg1, xbcp);
  mv(c_rarg2, xmethod);
  movptr(t0, CAST_FROM_FN_PTR(intptr_t, MacroAssembler::debug64));
  jalr(t0);
  ebreak();
}
