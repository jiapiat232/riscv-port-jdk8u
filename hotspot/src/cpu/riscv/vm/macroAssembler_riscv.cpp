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
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "memory/barrierSet.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "memory/metaspace.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/method.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.hpp"

// -------------------------------------------------------------
// 64-bit Pointer and Constant Loading (movptr vs li)
// -------------------------------------------------------------
// Architectural Note:
// - movptr(dst, val) MUST emit a fixed-length 6-instruction (24-byte) patchable
//   sequence (lui + addi + slli(11) + addi + slli(6) + addi) so that runtime
//   relocations and NativeMovConstReg::set_data can safely patch 48-bit pointers
//   even when compiled initially with NULL/0 placeholders.
// - li(dst, val) emits variable-length (1..6 instructions) optimized immediate
//   loading for non-patchable constants.
void MacroAssembler::movptr_with_offset(Register Rd, address addr, int32_t &offset) {
  int64_t imm64 = (int64_t)addr;
  #ifndef PRODUCT
  {
    // Check that address fits in 47-bit unsigned range or is -1 (Universe::non_oop_word)
    assert(((uint64_t)imm64 >> 47) == 0 || imm64 == (int64_t)-1,
           "bit 47 overflows in address constant for movptr");
  }
  #endif
  // Load upper 31 bits (use signed shift so imm64 == -1 produces imm = -1, upper = 0, lower = -1)
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

  // This 6-bit offset will be used by following addi/jalr/ld.
  offset = imm64 & 0x3f;
}

void MacroAssembler::movptr(Register dst, intptr_t val) {
  int32_t offset = 0;
  movptr_with_offset(dst, (address)val, offset);
  addi(dst, dst, offset);
}

void MacroAssembler::li(Register Rd, int64_t imm) {
  // int64_t is in range -2048 ~ 2047 (12 bits)
  if (is_simm12(imm)) {
    addi(Rd, zr, (int32_t)imm);
    return;
  }

  // int64_t is in range -2147483648 ~ 2147483647 (32 bits)
  if (is_simm32(imm)) {
    int32_t upper = (int32_t)imm, lower = (int32_t)imm;
    lower = (lower << 20) >> 20;
    upper -= lower;
    lui(Rd, upper);
    if (lower != 0) {
      addiw(Rd, Rd, lower);
    }
    return;
  }

  // Full 64-bit constant decomposition (matches JDK 19 Assembler::_li)
  int64_t upper = imm, lower = imm;
  lower = ((int32_t)imm << 20) >> 20;
  upper -= lower;

  int shift = 12;
  while (((upper >> shift) & 1) == 0) {
    shift++;
  }

  li(Rd, upper >> shift);
  slli(Rd, Rd, shift);
  if (lower != 0) {
    addi(Rd, Rd, lower);
  }
}

void MacroAssembler::mv(Register Rd, Address dest) {
  assert(dest.getMode() == Address::literal, "address mode should be literal");
  InstructionMark im(this);
  code_section()->relocate(pc(), dest.rspec());
  movptr(Rd, (intptr_t)dest.target());
}

// -------------------------------------------------------------
// Memory Operations using Address
// -------------------------------------------------------------
void MacroAssembler::ld(Register dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::ld(dst, adr.base(), adr.offset());
  } else {
    Register scratch = (dst == adr.base()) ? tmp : dst;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::ld(dst, scratch, 0);
  }
}

void MacroAssembler::sd(Register src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::sd(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(src, tmp);
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sd(src, tmp, 0);
  }
}

void MacroAssembler::lw(Register dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lw(dst, adr.base(), adr.offset());
  } else {
    Register scratch = (dst == adr.base()) ? tmp : dst;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lw(dst, scratch, 0);
  }
}

void MacroAssembler::sw(Register src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::sw(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(src, tmp);
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sw(src, tmp, 0);
  }
}

void MacroAssembler::lh(Register dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lh(dst, adr.base(), adr.offset());
  } else {
    Register scratch = (dst == adr.base()) ? tmp : dst;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lh(dst, scratch, 0);
  }
}

void MacroAssembler::sh(Register src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::sh(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(src, tmp);
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sh(src, tmp, 0);
  }
}

void MacroAssembler::lb(Register dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lb(dst, adr.base(), adr.offset());
  } else {
    Register scratch = (dst == adr.base()) ? tmp : dst;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lb(dst, scratch, 0);
  }
}

void MacroAssembler::sb(Register src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::sb(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(src, tmp);
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::sb(src, tmp, 0);
  }
}

void MacroAssembler::lbu(Register rd, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lbu(rd, adr.base(), adr.offset());
  } else {
    Register scratch = (rd == adr.base()) ? tmp : rd;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lbu(rd, scratch, 0);
  }
}

void MacroAssembler::lhu(Register rd, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lhu(rd, adr.base(), adr.offset());
  } else {
    Register scratch = (rd == adr.base()) ? tmp : rd;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lhu(rd, scratch, 0);
  }
}

void MacroAssembler::lwu(Register rd, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::lwu(rd, adr.base(), adr.offset());
  } else {
    Register scratch = (rd == adr.base()) ? tmp : rd;
    assert_different_registers(scratch, adr.base());
    li(scratch, adr.offset());
    add(scratch, adr.base(), scratch);
    Assembler::lwu(rd, scratch, 0);
  }
}

void MacroAssembler::fld(FloatRegister dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::fld(dst, adr.base(), adr.offset());
  } else {
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::fld(dst, tmp, 0);
  }
}

void MacroAssembler::fsd(FloatRegister src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::fsd(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::fsd(src, tmp, 0);
  }
}

void MacroAssembler::flw(FloatRegister dst, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::flw(dst, adr.base(), adr.offset());
  } else {
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
    add(tmp, adr.base(), tmp);
    Assembler::flw(dst, tmp, 0);
  }
}

void MacroAssembler::fsw(FloatRegister src, Address adr, Register tmp) {
  if (adr.is_simm12()) {
    Assembler::fsw(src, adr.base(), adr.offset());
  } else {
    assert_different_registers(adr.base(), tmp);
    li(tmp, adr.offset());
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
  // stack grows down, caller passes positive offset
  assert(offset > 0, "must bang with positive offset");
  if (offset <= 2048) {
    sd(zr, Address(sp, -offset));
  } else {
    li(t0, offset);
    sub(t0, sp, t0);
    sd(zr, Address(t0, 0));
  }
}

// Writes to stack successive pages until offset reached to check for
// stack overflow + shadow pages. This clobbers tmp.
void MacroAssembler::bang_stack_size(Register size, Register tmp) {
  assert_different_registers(tmp, size, t0);
  mv(tmp, sp);
  Label loop;
  li(t0, os::vm_page_size());
  bind(loop);
  sub(tmp, tmp, t0);
  subw(size, size, t0);
  sd(size, Address(tmp, 0));
  bgtz(size, loop);

  // Bang down shadow pages too.
  // The -1 because we already subtracted 1 page.
  for (int i = 0; i < StackShadowPages - 1; i++) {
    sub(tmp, tmp, t0);
    sd(size, Address(tmp, 0));
  }
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

int MacroAssembler::bitset_to_regs(unsigned int bitset, unsigned char* regs) {
  int count = 0;
  // Scan bitset to accumulate register indices (0..31)
  for (int reg = 31; reg >= 0; reg--) {
    if ((1U << 31) & bitset) {
      regs[count++] = reg;
    }
    bitset <<= 1;
  }
  return count;
}

// Push registers in the bitset supplied to the specified stack (e.g., sp).
// Reserves an extra word slot when count is odd to guarantee 16-byte stack alignment per RISC-V psABI.
int MacroAssembler::push_reg(unsigned int bitset, Register stack) {
  DEBUG_ONLY(int words_pushed = 0;)

  unsigned char regs[32];
  int count = bitset_to_regs(bitset, regs);
  int offset = is_even(count) ? 0 : wordSize;

  if (count) {
    subi(stack, stack, count * wordSize + offset);
  }
  for (int i = count - 1; i >= 0; i--) {
    sd(as_Register(regs[i]), Address(stack, (count - 1 - i) * wordSize + offset));
    DEBUG_ONLY(words_pushed++;)
  }

  assert(words_pushed == count, "oops, pushed != count");
  return count;
}

int MacroAssembler::pop_reg(unsigned int bitset, Register stack) {
  DEBUG_ONLY(int words_popped = 0;)

  unsigned char regs[32];
  int count = bitset_to_regs(bitset, regs);
  int offset = is_even(count) ? 0 : wordSize;

  for (int i = count - 1; i >= 0; i--) {
    ld(as_Register(regs[i]), Address(stack, (count - 1 - i) * wordSize + offset));
    DEBUG_ONLY(words_popped++;)
  }

  if (count) {
    addi(stack, stack, count * wordSize + offset);
  }
  assert(words_popped == count, "oops, popped != count");
  return count;
}

void MacroAssembler::push_call_clobbered_registers_except(RegSet exclude) {
  // Push integer registers x7, x10-x17, x28-x31.
  push_reg(RegSet::of(x7) + RegSet::range(x10, x17) + RegSet::range(x28, x31) - exclude, sp);

  // Push float registers f0-f7, f10-f17, f28-f31 (20 caller-saved float regs).
  subi(sp, sp, wordSize * 20);
  int offset = 0;
  for (int i = 0; i < 32; i++) {
    if (i <= f7->encoding() || i >= f28->encoding() || (i >= f10->encoding() && i <= f17->encoding())) {
      fsd(as_FloatRegister(i), Address(sp, wordSize * (offset++)));
    }
  }
}

void MacroAssembler::pop_call_clobbered_registers_except(RegSet exclude) {
  int offset = 0;
  for (int i = 0; i < 32; i++) {
    if (i <= f7->encoding() || i >= f28->encoding() || (i >= f10->encoding() && i <= f17->encoding())) {
      fld(as_FloatRegister(i), Address(sp, wordSize * (offset++)));
    }
  }
  addi(sp, sp, wordSize * 20);

  pop_reg(RegSet::of(x7) + RegSet::range(x10, x17) + RegSet::range(x28, x31) - exclude, sp);
}

void MacroAssembler::push_CPU_state() {
  // integer registers, except zr(x0) & ra(x1) & sp(x2) & gp(x3) & tp(x4)
  push_reg(0xffffffe0, sp);

  // float registers
  subi(sp, sp, 32 * wordSize);
  for (int i = 0; i < 32; i++) {
    fsd(as_FloatRegister(i), Address(sp, i * wordSize));
  }
}

void MacroAssembler::pop_CPU_state() {
  // float registers
  for (int i = 0; i < 32; i++) {
    fld(as_FloatRegister(i), Address(sp, i * wordSize));
  }
  addi(sp, sp, 32 * wordSize);

  // integer registers, except zr(x0) & ra(x1) & sp(x2) & gp(x3) & tp(x4)
  pop_reg(0xffffffe0, sp);
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
// Calls, Far Branches, Trampolines, and Inline Cache Calls
// -------------------------------------------------------------
void MacroAssembler::call(address entry) {
  movptr(t0, (intptr_t)entry);
  jalr(ra, t0, 0);
}

void MacroAssembler::jmp(address entry) {
  movptr(t0, (intptr_t)entry);
  jalr(zr, t0, 0);
}

void MacroAssembler::la_patchable(Register reg1, const Address &dest, int32_t &offset) {
  relocInfo::relocType rtype = dest.rspec().reloc()->type();
  assert(dest.getMode() == Address::literal, "la_patchable must be applied to a literal address");
  InstructionMark im(this);
  code_section()->relocate(pc(), dest.rspec());
  if (rtype != relocInfo::none &&
      CodeCache::contains(dest.target()) &&
      is_offset_in_range(dest.target() - pc(), 32)) {
    int64_t distance = dest.target() - pc();
    auipc(reg1, (int32_t)distance + 0x800);
    offset = ((int32_t)distance << 20) >> 20;
  } else {
    movptr_with_offset(reg1, dest.target(), offset);
  }
}

void MacroAssembler::far_jump(const Address& entry, CodeBuffer* cbuf, Register tmp) {
  assert(ReservedCodeCacheSize < 4*G, "branch out of range");
  if (entry.getMode() == Address::literal) {
    assert(CodeCache::find_blob(entry.target()) != NULL || !far_branches() || entry.target() != NULL,
           "destination of far jump must be valid");
    int32_t offset = 0;
    if (far_branches() || !CodeCache::contains(entry.target()) || !reachable_from_branch_at(pc(), entry.target())) {
      la_patchable(tmp, entry, offset);
      if (cbuf != NULL) { cbuf->set_insts_mark(); }
      jalr(zr, tmp, offset);
    } else {
      if (cbuf != NULL) { cbuf->set_insts_mark(); }
      InstructionMark im(this);
      code_section()->relocate(pc(), entry.rspec());
      int32_t distance = (int32_t)(entry.target() - pc());
      Assembler::jal(zr, distance);
    }
  } else {
    movptr(tmp, entry.offset());
    add(tmp, entry.base(), tmp);
    if (cbuf != NULL) { cbuf->set_insts_mark(); }
    jalr(zr, tmp, 0);
  }
}

void MacroAssembler::far_call(const Address& entry, CodeBuffer* cbuf, Register tmp) {
  assert(ReservedCodeCacheSize < 4*G, "branch out of range");
  if (entry.getMode() == Address::literal) {
    int32_t offset = 0;
    if (far_branches() || !CodeCache::contains(entry.target()) || !reachable_from_branch_at(pc(), entry.target())) {
      la_patchable(tmp, entry, offset);
      if (cbuf != NULL) { cbuf->set_insts_mark(); }
      jalr(ra, tmp, offset);
    } else {
      if (cbuf != NULL) { cbuf->set_insts_mark(); }
      InstructionMark im(this);
      code_section()->relocate(pc(), entry.rspec());
      int32_t distance = (int32_t)(entry.target() - pc());
      Assembler::jal(ra, distance);
    }
  } else {
    movptr(tmp, entry.offset());
    add(tmp, entry.base(), tmp);
    if (cbuf != NULL) { cbuf->set_insts_mark(); }
    jalr(ra, tmp, 0);
  }
}

address MacroAssembler::trampoline_call(Address entry, CodeBuffer* cbuf) {
  assert(entry.rspec().type() == relocInfo::runtime_call_type ||
         entry.rspec().type() == relocInfo::opt_virtual_call_type ||
         entry.rspec().type() == relocInfo::static_call_type ||
         entry.rspec().type() == relocInfo::virtual_call_type, "wrong reloc type");

  // Look up current Compile/C1 stub section if cbuf is provided
  if (far_branches()) {
    address stub = emit_trampoline_stub(offset(), entry.target());
    if (stub == NULL) {
      return NULL; // CodeCache / stub section is full
    }
  }

  if (cbuf != NULL) {
    cbuf->set_insts_mark();
  }
  InstructionMark im(this);
  relocate(entry.rspec());
  if (!far_branches() && reachable_from_branch_at(pc(), entry.target())) {
    Assembler::jal(ra, (int32_t)(entry.target() - pc()));
  } else {
    // Emit a self-jump placeholder; Relocation::pd_call_destination and
    // fix_relocation_after_move will link it to the trampoline stub.
    Assembler::jal(ra, (int32_t)0);
  }
  return pc();
}

address MacroAssembler::ic_call(address entry, jint method_index) {
  RelocationHolder rh = virtual_call_Relocation::spec(pc());
  movptr(xmethod, (intptr_t)Universe::non_oop_word());
  return trampoline_call(Address(entry, rh));
}

// Emit a trampoline stub for a call to dest at insts_call_instruction_offset.
address MacroAssembler::emit_trampoline_stub(int insts_call_instruction_offset, address dest) {
  // Start the stub
  address stub = start_a_stub(NativeCallTrampolineStub::instruction_size);
  if (stub == NULL) {
    return NULL;  // CodeBuffer::expand failed
  }

  // Create a trampoline stub relocation which relates this trampoline stub
  // with the call instruction at insts_call_instruction_offset in the
  // instructions code-section.
  align(wordSize);
  relocate(trampoline_stub_Relocation::spec(code()->insts()->start() +
                                            insts_call_instruction_offset));
  const int stub_start_offset = offset();

  // Layout (20 bytes total = 3 instructions + 8-byte embedded 64-bit target):
  //   0: auipc t0, 0
  //   4: ld    t0, 12(t0)
  //   8: jr    t0
  //  12: .dword dest
  Label target_addr;
  ld(t0, target_addr);
  jr(t0);
  assert((offset() - stub_start_offset) % wordSize == 4, "must be 4-byte offset before target_addr");
  bind(target_addr);
  emit_int64((int64_t)dest);

  assert(offset() - stub_start_offset == NativeCallTrampolineStub::instruction_size,
         "stub_size should be NativeCallTrampolineStub::instruction_size");
  end_a_stub();
  return stub;
}

void MacroAssembler::emit_static_call_stub() {
  // CompiledStaticCall::set_to_interpreted knows the exact layout of this stub:
  //   0: fence.i                           (4 bytes)
  //   4: movptr(xmethod, 0)                (24 bytes, 6 instructions)
  //  28: movptr_with_offset(t0, 0) + jalr  (24 bytes, 6 instructions)
  ifence();
  mov_metadata(xmethod, (Metadata*)NULL);

  // Jump to the entry point of the i2c stub.
  int32_t offset = 0;
  movptr_with_offset(t0, 0, offset);
  jalr(zr, t0, offset);
}

int MacroAssembler::static_call_stub_size() {
  // (1 or 2 for ifence) + 6 (mov_metadata) + 6 (movptr_with_offset + jalr) = 13 or 14 instructions
  return (NativeFenceI::instruction_size() +
          2 * NativeMovConstReg::movptr_instruction_size);
}

void MacroAssembler::ld(Register Rd, Label& L) {
  if (L.is_bound()) {
    int64_t distance = target(L) - pc();
    assert(is_offset_in_range(distance, 32), "distance out of 32-bit pc-relative range");
    auipc(Rd, (int32_t)distance + 0x800);
    int32_t offset = ((int32_t)distance << 20) >> 20;
    Assembler::ld(Rd, Rd, offset);
  } else {
    L.add_patch_at(code(), locator());
    auipc(Rd, 0);
    Assembler::ld(Rd, Rd, 0);
  }
}

void MacroAssembler::la(Register Rd, const Address& adr) {
  if (adr.getMode() == Address::literal) {
    InstructionMark im(this);
    code_section()->relocate(pc(), adr.rspec());
    movptr(Rd, (intptr_t)adr.target());
  } else {
    if (adr.is_simm12()) {
      addi(Rd, adr.base(), adr.offset());
    } else {
      li(Rd, adr.offset());
      add(Rd, adr.base(), Rd);
    }
  }
}

void MacroAssembler::la(Register Rd, const address addr) {
  int64_t offset = addr - pc();
  if (is_offset_in_range(offset, 32)) {
    auipc(Rd, (int32_t)offset + 0x800);
    addi(Rd, Rd, ((int64_t)offset << 52) >> 52);
  } else {
    movptr(Rd, (intptr_t)addr);
  }
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
void MacroAssembler::beq(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::bne(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::beq(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::beq(rs1, rs2, 0);
  }
}

void MacroAssembler::bne(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::beq(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bne(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bne(rs1, rs2, 0);
  }
}

void MacroAssembler::blt(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::bge(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::blt(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::blt(rs1, rs2, 0);
  }
}

void MacroAssembler::bge(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::blt(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bge(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bge(rs1, rs2, 0);
  }
}

void MacroAssembler::bltu(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::bgeu(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bltu(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bltu(rs1, rs2, 0);
  }
}

void MacroAssembler::bgeu(Register rs1, Register rs2, Label& L, bool is_far) {
  if (is_far) {
    Label done;
    Assembler::bltu(rs1, rs2, 8);
    j(L);
    bind(done);
    return;
  }
  if (L.is_bound()) {
    int32_t offset = target(L) - pc();
    Assembler::bgeu(rs1, rs2, offset);
  } else {
    L.add_patch_at(code(), locator());
    Assembler::bgeu(rs1, rs2, 0);
  }
}

// Float compare branch instructions
#define INSN(NAME, FLOATCMP, BRANCH)                                                                                   \
  void MacroAssembler::float_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l, bool is_far, bool is_unordered) {  \
    FLOATCMP##_s(t0, Rs1, Rs2);                                                                                        \
    BRANCH(t0, l, is_far);                                                                                             \
  }                                                                                                                    \
  void MacroAssembler::double_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l, bool is_far, bool is_unordered) { \
    FLOATCMP##_d(t0, Rs1, Rs2);                                                                                        \
    BRANCH(t0, l, is_far);                                                                                             \
  }

  INSN(beq, feq, bnez);
  INSN(bne, feq, beqz);

#undef INSN

#define INSN(NAME, FLOATCMP1, FLOATCMP2)                                              \
  void MacroAssembler::float_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l,   \
                                    bool is_far, bool is_unordered) {                 \
    if (is_unordered) {                                                               \
      FLOATCMP2##_s(t0, Rs2, Rs1);                                                    \
      beqz(t0, l, is_far);                                                            \
    } else {                                                                          \
      FLOATCMP1##_s(t0, Rs1, Rs2);                                                    \
      bnez(t0, l, is_far);                                                            \
    }                                                                                 \
  }                                                                                   \
  void MacroAssembler::double_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l,  \
                                     bool is_far, bool is_unordered) {                \
    if (is_unordered) {                                                               \
      FLOATCMP2##_d(t0, Rs2, Rs1);                                                    \
      beqz(t0, l, is_far);                                                            \
    } else {                                                                          \
      FLOATCMP1##_d(t0, Rs1, Rs2);                                                    \
      bnez(t0, l, is_far);                                                            \
    }                                                                                 \
  }

  INSN(ble, fle, flt);
  INSN(blt, flt, fle);

#undef INSN

#define INSN(NAME, CMP)                                                              \
  void MacroAssembler::float_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l,  \
                                    bool is_far, bool is_unordered) {                \
    float_##CMP(Rs2, Rs1, l, is_far, is_unordered);                                  \
  }                                                                                  \
  void MacroAssembler::double_##NAME(FloatRegister Rs1, FloatRegister Rs2, Label &l, \
                                     bool is_far, bool is_unordered) {               \
    double_##CMP(Rs2, Rs1, l, is_far, is_unordered);                                 \
  }

  INSN(bgt, blt);
  INSN(bge, ble);

#undef INSN

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

// -------------------------------------------------------------
// Instruction Patching and Target Extraction
// -------------------------------------------------------------
static int patch_offset_in_conditional_branch(address branch, int64_t offset) {
  assert(Assembler::is_simm13(offset) && (offset & 1) == 0, "branch offset out of range");
  Assembler::patch(branch, 31, 31, (offset >> 12) & 0x1);
  Assembler::patch(branch, 30, 25, (offset >> 5) & 0x3f);
  Assembler::patch(branch, 11, 8, (offset >> 1) & 0xf);
  Assembler::patch(branch, 7, 7, (offset >> 11) & 0x1);
  return NativeInstruction::instruction_size;
}

static int patch_offset_in_jal(address branch, int64_t offset) {
  assert(Assembler::is_simm21(offset) && (offset & 1) == 0, "jal offset out of range");
  Assembler::patch(branch, 31, 31, (offset >> 20) & 0x1);
  Assembler::patch(branch, 30, 21, (offset >> 1) & 0x3ff);
  Assembler::patch(branch, 20, 20, (offset >> 11) & 0x1);
  Assembler::patch(branch, 19, 12, (offset >> 12) & 0xff);
  return NativeInstruction::instruction_size;
}

static int patch_offset_in_pc_relative(address branch, int64_t offset) {
  assert(Assembler::is_offset_in_range(offset, 32), "pc-relative offset out of 32-bit range");
  const int32_t upper = (((int32_t)offset + 0x800) >> 12) & 0xfffff;
  const int32_t lower = offset & 0xfff;
  Assembler::patch(branch, 31, 12, upper);                            // auipc
  Assembler::patch(branch + NativeInstruction::instruction_size, 31, 20, lower); // addi/jalr/ld/lw/lwu/flw/fld
  return 2 * NativeInstruction::instruction_size;
}

static int patch_addr_in_movptr(address branch, address target) {
  int32_t lower = ((intptr_t)target << 35) >> 35;
  int64_t upper = ((intptr_t)target - lower) >> 29;
  Assembler::patch(branch + 0,  31, 12, upper & 0xfffff);                // lui
  Assembler::patch(branch + 4,  31, 20, (lower >> 17) & 0xfff);          // addi
  // branch + 8 is slli(rd, rd, 11)
  Assembler::patch(branch + 12, 31, 20, (lower >> 6) & 0x7ff);           // addi
  // branch + 16 is slli(rd, rd, 6)
  Assembler::patch(branch + 20, 31, 20, lower & 0x3f);                   // addi/jalr/ld
  return 6 * NativeInstruction::instruction_size;
}

static int patch_imm_in_li32(address branch, int32_t target) {
  int64_t imm32 = (int32_t)target;
  int64_t upper = imm32, lower = imm32;
  lower = (lower << 52) >> 52;
  upper -= lower;
  upper = (int32_t)upper;
  Assembler::patch(branch, 31, 12, (upper >> 12) & 0xfffff);          // lui
  Assembler::patch(branch + 4, 31, 20, lower & 0xfff);                // addiw
  return 2 * NativeInstruction::instruction_size;
}

static long get_offset_of_conditional_branch(address insn_addr) {
  long offset = 0;
  offset |= Assembler::sextract(((unsigned*)insn_addr)[0], 31, 31) << 12;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 7, 7) << 11;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 30, 25) << 5;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 11, 8) << 1;
  return (offset << 51) >> 51;
}

static long get_offset_of_jal(address insn_addr) {
  assert(insn_addr != NULL, "sanity");
  long offset = 0;
  offset |= Assembler::sextract(((unsigned*)insn_addr)[0], 31, 31) << 20;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 19, 12) << 12;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 20, 20) << 11;
  offset |= Assembler::extract(((unsigned*)insn_addr)[0], 30, 21) << 1;
  return (offset << 43) >> 43;
}

static long get_offset_of_pc_relative(address insn_addr) {
  long offset = Assembler::sextract(((unsigned*)insn_addr)[0], 31, 12) << 12; // auipc
  offset += Assembler::sextract(((unsigned*)insn_addr)[1], 31, 20);           // addi/jalr/ld/lw/lwu/flw/fld
  return (offset << 32) >> 32;
}

static address get_target_of_movptr(address insn_addr) {
  intptr_t target_address = (((int64_t)Assembler::sextract(((unsigned*)insn_addr)[0], 31, 12)) & 0xfffff) << 29; // lui
  target_address += ((int64_t)Assembler::sextract(((unsigned*)insn_addr)[1], 31, 20)) << 17;                     // addi
  target_address += ((int64_t)Assembler::sextract(((unsigned*)insn_addr)[3], 31, 20)) << 6;                      // addi
  target_address += ((int64_t)Assembler::sextract(((unsigned*)insn_addr)[5], 31, 20));                           // addi/jalr/ld
  return (address)target_address;
}

static address get_target_of_li32(address insn_addr) {
  int64_t target = Assembler::sextract(((unsigned*)insn_addr)[0], 31, 12) << 12; // lui
  target += Assembler::sextract(((unsigned*)insn_addr)[1], 31, 20);              // addiw
  return (address)target;
}

int MacroAssembler::pd_patch_instruction_size(address branch, address target) {
  assert(branch != NULL, "sanity");
  if (NativeInstruction::is_jal_at(branch)) {
    int64_t offset = target - branch;
    return patch_offset_in_jal(branch, offset);
  } else if (NativeInstruction::is_branch_at(branch)) {
    int64_t offset = target - branch;
    return patch_offset_in_conditional_branch(branch, offset);
  } else if (NativeInstruction::is_pc_relative_at(branch)) {
    int64_t offset = target - branch;
    return patch_offset_in_pc_relative(branch, offset);
  } else if (NativeInstruction::is_movptr_at(branch)) {
    return patch_addr_in_movptr(branch, target);
  } else if (NativeInstruction::is_li32_at(branch)) {
    int64_t imm = (int64_t)target;
    return patch_imm_in_li32(branch, (int32_t)imm);
  } else if (NativeInstruction::is_lwu_to_zr(branch)) {
    // Safepoint poll instruction (lwu zr, 0(reg)) has poll_type relocation; no-op on move.
    return NativeInstruction::instruction_size;
  } else {
    tty->print_cr("pd_patch_instruction_size: unknown instruction at " INTPTR_FORMAT " (0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x)",
                  p2i(branch),
                  ((uint32_t*)branch)[0], ((uint32_t*)branch)[1],
                  ((uint32_t*)branch)[2], ((uint32_t*)branch)[3],
                  ((uint32_t*)branch)[4], ((uint32_t*)branch)[5]);
    ShouldNotReachHere();
    return -1;
  }
}

address MacroAssembler::target_addr_for_insn(address insn_addr) {
  assert(insn_addr != NULL, "sanity");
  long offset = 0;
  if (NativeInstruction::is_jal_at(insn_addr)) {
    offset = get_offset_of_jal(insn_addr);
  } else if (NativeInstruction::is_branch_at(insn_addr)) {
    offset = get_offset_of_conditional_branch(insn_addr);
  } else if (NativeInstruction::is_pc_relative_at(insn_addr)) {
    offset = get_offset_of_pc_relative(insn_addr);
  } else if (NativeInstruction::is_movptr_at(insn_addr)) {
    return get_target_of_movptr(insn_addr);
  } else if (NativeInstruction::is_li32_at(insn_addr)) {
    return get_target_of_li32(insn_addr);
  } else if (NativeInstruction::is_lwu_to_zr(insn_addr)) {
    return (address)os::get_polling_page();
  } else {
    tty->print_cr("target_addr_for_insn: unknown instruction at " INTPTR_FORMAT " (0x%08x)",
                  p2i(insn_addr), *(uint32_t*)insn_addr);
    ShouldNotReachHere();
  }
  return address(((uintptr_t)insn_addr + offset));
}

int MacroAssembler::patch_oop(address insn_addr, address o) {
  // OOPs are either loaded with 6-instruction movptr (uncompressed) or 2-instruction li32 (compressed narrowOop)
  if (NativeInstruction::is_movptr_at(insn_addr)) {
    return patch_addr_in_movptr(insn_addr, o);
  } else if (NativeInstruction::is_li32_at(insn_addr)) {
    narrowOop n = oopDesc::encode_heap_oop((oop)o);
    return patch_imm_in_li32(insn_addr, (int32_t)n);
  } else {
    ShouldNotReachHere();
    return -1;
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
  assert(bs->is_a(BarrierSet::CardTableModRef), "wrong barrier set kind");
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
  lwu(zr, Address(tmp, 0));
}

void MacroAssembler::get_polling_page(Register dest, address page, relocInfo::relocType rtype) {
  movptr(dest, (intptr_t)page);
}

address MacroAssembler::read_polling_page(Register r, address page, relocInfo::relocType rtype) {
  get_polling_page(r, page, rtype);
  return read_polling_page(r, rtype);
}

address MacroAssembler::read_polling_page(Register r, relocInfo::relocType rtype) {
  return read_polling_page(r, 0, rtype);
}

address MacroAssembler::read_polling_page(Register r, int32_t offset, relocInfo::relocType rtype) {
  InstructionMark im(this);
  code_section()->relocate(inst_mark(), rtype);
  lwu(zr, Address(r, offset));
  return inst_mark();
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

Address MacroAssembler::form_address(Register Rd, Register base, long byte_offset) {
  if (is_simm12(byte_offset)) {
    return Address(base, byte_offset);
  }
  mv(Rd, byte_offset);
  add(Rd, base, Rd);
  return Address(Rd, 0);
}

void MacroAssembler::membar(uint32_t order_constraint) {
  if (order_constraint == 0) return;
  uint32_t predecessor = 0;
  uint32_t successor = 0;
  membar_mask_to_pred_succ(order_constraint, predecessor, successor);
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
// [JDK8u RISC-V Port Backport Note]
// In JDK 11+ (JEP 304), G1 barriers were moved to G1BarrierSetAssembler.
// In JDK 8u, they live directly in MacroAssembler (same as AArch64 port).
// Allocates t0 internally as scratch; callers must pass tmp != t0.
void MacroAssembler::g1_write_barrier_pre(Register obj,
                                          Register pre_val,
                                          Register thread,
                                          Register tmp,
                                          bool tosca_live,
                                          bool expand_call) {
  // If expand_call is true then we expand the call_VM_leaf macro
  // directly to skip generating the check by
  // InterpreterMacroAssembler::call_VM_leaf_base that checks _last_sp.
  assert(thread == xthread, "must be");

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

  // Do we need to load the previous value?
  if (obj != noreg) {
    load_heap_oop(pre_val, Address(obj, 0));
  }
  // Is the previous value null?
  beqz(pre_val, done);

  // Can we store original value in the thread's SATB queue buffer?
  ld(tmp, index);
  beqz(tmp, runtime);
  subi(tmp, tmp, wordSize);
  sd(tmp, index);
  ld(t0, buffer);
  add(t0, t0, tmp);
  sd(pre_val, Address(t0, 0));
  j(done);

  bind(runtime);
  // Save live input values across C runtime leaf call (16-byte aligned via push_reg)
  RegSet saved = RegSet::of(pre_val);
  if (tosca_live) { saved += RegSet::of(x10); }
  if (obj != noreg) { saved += RegSet::of(obj); }

  push_reg(saved, sp);

  // Calling the runtime using the regular call_VM_leaf mechanism generates
  // code (generated by InterpreterMacroAssembler::call_VM_leaf_base)
  // that checks that the *(fp+frame::interpreter_frame_last_sp) == NULL.
  // If we are generating the pre-barrier without a full interpreter frame
  // (e.g. in the intrinsified Reference.get() routine) then fp might be
  // pointing to the caller frame and so this check will fail at runtime.
  // Expanding the call directly bypasses the generation of the check.
  if (expand_call) {
    assert(pre_val != c_rarg1, "smashed arg");
    mv(c_rarg1, thread);
    mv(c_rarg0, pre_val);
    MacroAssembler::call_VM_leaf_base(CAST_FROM_FN_PTR(address, SharedRuntime::g1_wb_pre), 2);
  } else {
    call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::g1_wb_pre), pre_val, thread);
  }

  pop_reg(saved, sp);

  bind(done);
}

// G1GC post-write barrier for store of new_val at store_addr.
// Allocates t0 internally as scratch; callers must ensure store_addr, new_val, tmp, tmp2 != t0.
void MacroAssembler::g1_write_barrier_post(Register store_addr,
                                           Register new_val,
                                           Register thread,
                                           Register tmp,
                                           Register tmp2) {
  assert(thread == xthread, "must be");
  assert_different_registers(store_addr, new_val, thread, tmp, tmp2, t0);
  assert(store_addr != noreg && new_val != noreg && tmp != noreg &&
         tmp2 != noreg, "expecting a register");

  Label done;
  Label runtime;

  // Does store cross heap regions?
  xorr(tmp, store_addr, new_val);
  srli(tmp, tmp, HeapRegion::LogOfHRGrainBytes);
  beqz(tmp, done);

  // Crosses regions, storing NULL?
  beqz(new_val, done);

  // Storing region crossing non-NULL, is card already dirty?
  const Register card_addr = tmp;
  srli(card_addr, store_addr, CardTableModRefBS::card_shift);

  load_byte_map_base(tmp2);
  add(card_addr, card_addr, tmp2);
  lbu(tmp2, Address(card_addr, 0));
  mv(t0, (int)G1SATBCardTableModRefBS::g1_young_card_val());
  beq(tmp2, t0, done);

  assert((int)CardTableModRefBS::dirty_card_val() == 0, "must be 0");
  membar(StoreLoad);

  lbu(tmp2, Address(card_addr, 0));
  beqz(tmp2, done);

  // Storing a region crossing, non-NULL oop, card is clean.
  // Dirty card and log in thread dirty card queue.
  sb(zr, Address(card_addr, 0));

  Address card_index(thread, in_bytes(JavaThread::dirty_card_queue_offset() +
                                      PtrQueue::byte_offset_of_index()));
  Address card_buffer(thread, in_bytes(JavaThread::dirty_card_queue_offset() +
                                       PtrQueue::byte_offset_of_buf()));

  ld(t0, card_index);
  beqz(t0, runtime);
  subi(t0, t0, wordSize);
  sd(t0, card_index);
  ld(tmp2, card_buffer);
  add(t0, tmp2, t0);
  sd(card_addr, Address(t0, 0));
  j(done);

  bind(runtime);
  // Save live input values across C runtime leaf call
  RegSet saved = RegSet::of(store_addr);
  push_reg(saved, sp);
  call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::g1_wb_post), card_addr, thread);
  pop_reg(saved, sp);

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
    la(tmp, last_java_pc);
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
void MacroAssembler::encode_klass_not_null(Register r) {
  encode_klass_not_null(r, r);
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

void MacroAssembler::decode_klass_not_null(Register r) {
  decode_klass_not_null(r, r);
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

void MacroAssembler::load_prototype_header(Register dst, Register src) {
  load_klass(dst, src);
  ld(dst, Address(dst, Klass::prototype_header_offset()));
}

void MacroAssembler::cmp_klass(Register oop, Register trial_klass, Register tmp, Label &L) {
  if (UseCompressedClassPointers) {
    lwu(tmp, Address(oop, oopDesc::klass_offset_in_bytes()));
    if (Universe::narrow_klass_base() == NULL) {
      if (Universe::narrow_klass_shift() != 0) {
        slli(tmp, tmp, Universe::narrow_klass_shift());
      }
      beq(trial_klass, tmp, L);
      return;
    }
    decode_klass_not_null(tmp);
  } else {
    ld(tmp, Address(oop, oopDesc::klass_offset_in_bytes()));
  }
  beq(trial_klass, tmp, L);
}

// -------------------------------------------------------------
// Atomic CAS & Biased Locking (JDK 8u Architecture + RISC-V LR/SC)
// -------------------------------------------------------------
// Compare-and-exchange pointer: compares [addr] with oldv; if equal, stores newv
// and branches to succeed. If not equal, updates oldv with the loaded value from
// [addr] and branches to *fail (or falls through if fail == NULL).
void MacroAssembler::cmpxchgptr(Register oldv, Register newv, Register addr, Register tmp,
                                Label &succeed, Label *fail) {
  assert_different_registers(oldv, newv, addr, tmp);
  Label retry_load, nope;
  bind(retry_load);
  lr_d(tmp, addr, true /* aq */, true /* rl */);
  bne(tmp, oldv, nope);
  sc_d(tmp, newv, addr, false /* aq */, true /* rl */);
  beqz(tmp, succeed);
  j(retry_load);

  bind(nope);
  membar(AnyAny);
  mv(oldv, tmp);
  if (fail != NULL) {
    j(*fail);
  }
}

void MacroAssembler::cmpxchg_obj_header(Register oldv, Register newv, Register obj, Register tmp,
                                        Label &succeed, Label *fail) {
  assert(oopDesc::mark_offset_in_bytes() == 0, "assumption");
  cmpxchgptr(oldv, newv, obj, tmp, succeed, fail);
}

// Biased locking was removed in JDK 15+ (JEP 374) and is absent in JDK 19 RISC-V,
// but is enabled by default in JDK 8u (with BiasedLockingStartupDelay=4000ms).
// Ported from JDK 8u AArch64 (macroAssembler_aarch64.cpp:402-576) using RISC-V instructions.
int MacroAssembler::biased_locking_enter(Register lock_reg,
                                         Register obj_reg,
                                         Register swap_reg,
                                         Register tmp_reg,
                                         bool swap_reg_contains_mark,
                                         Label& done,
                                         Label* slow_case,
                                         BiasedLockingCounters* counters) {
  assert(UseBiasedLocking, "why call this otherwise?");
  assert_different_registers(lock_reg, obj_reg, swap_reg, tmp_reg, t0, t1, noreg);
  assert(markOopDesc::age_shift == markOopDesc::lock_bits + markOopDesc::biased_lock_bits,
         "biased locking makes assumptions about bit layout");

  Address mark_addr(obj_reg, oopDesc::mark_offset_in_bytes());

  Label cas_label;
  int null_check_offset = -1;
  if (!swap_reg_contains_mark) {
    null_check_offset = offset();
    ld(swap_reg, mark_addr);
  }
  andi(tmp_reg, swap_reg, markOopDesc::biased_lock_mask_in_place);
  subi(tmp_reg, tmp_reg, markOopDesc::biased_lock_pattern);
  bnez(tmp_reg, cas_label);

  // The bias pattern is present in the object's header. Need to check
  // whether the bias owner and the epoch are both still current.
  load_prototype_header(tmp_reg, obj_reg);
  orr(tmp_reg, tmp_reg, xthread);
  xorr(tmp_reg, swap_reg, tmp_reg);
  andi(tmp_reg, tmp_reg, ~((int) markOopDesc::age_mask_in_place));
  beqz(tmp_reg, done);

  Label try_revoke_bias;
  Label try_rebias;

  // If the low three bits in the xor result aren't clear, that means
  // the prototype header is no longer biased and we have to revoke
  // the bias on this object.
  andi(t0, tmp_reg, markOopDesc::biased_lock_mask_in_place);
  bnez(t0, try_revoke_bias);

  // Biasing is still enabled for this data type. See whether the
  // epoch of the current bias is still valid. If not, attempt to
  // rebias the object toward the current thread.
  andi(t0, tmp_reg, markOopDesc::epoch_mask_in_place);
  bnez(t0, try_rebias);

  // The epoch of the current bias is still valid but the owner is anonymous (0)
  // or another thread. Construct presumed unbiased header and CAS to acquire bias.
  {
    Label here;
    mv(t0, markOopDesc::biased_lock_mask_in_place |
           markOopDesc::age_mask_in_place |
           markOopDesc::epoch_mask_in_place);
    andr(swap_reg, swap_reg, t0);
    orr(tmp_reg, swap_reg, xthread);
    cmpxchg_obj_header(swap_reg, tmp_reg, obj_reg, t0, here, slow_case);
    bind(here);
  }
  j(done);

  bind(try_rebias);
  {
    Label here;
    load_prototype_header(tmp_reg, obj_reg);
    orr(tmp_reg, xthread, tmp_reg);
    cmpxchg_obj_header(swap_reg, tmp_reg, obj_reg, t0, here, slow_case);
    bind(here);
  }
  j(done);

  bind(try_revoke_bias);
  {
    Label here, nope;
    load_prototype_header(tmp_reg, obj_reg);
    cmpxchg_obj_header(swap_reg, tmp_reg, obj_reg, t0, here, &nope);
    bind(here);
    bind(nope);
  }

  bind(cas_label);
  return null_check_offset;
}

void MacroAssembler::biased_locking_exit(Register obj_reg, Register temp_reg, Label& done) {
  assert(UseBiasedLocking, "why call this otherwise?");

  // Check for biased locking unlock case, which is a no-op.
  ld(temp_reg, Address(obj_reg, oopDesc::mark_offset_in_bytes()));
  andi(temp_reg, temp_reg, markOopDesc::biased_lock_mask_in_place);
  subi(temp_reg, temp_reg, markOopDesc::biased_lock_pattern);
  beqz(temp_reg, done);
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
  check_klass_subtype_slow_path(sub_klass, super_klass, temp_reg, noreg, &L_success, NULL);
  bind(L_failure);
}

void MacroAssembler::check_klass_subtype_fast_path(Register sub_klass,
                                                   Register super_klass,
                                                   Register temp_reg,
                                                   Label* L_success,
                                                   Label* L_failure,
                                                   Label* L_slow_path,
                                                   Register super_check_offset) {
  assert_different_registers(sub_klass, super_klass, temp_reg);
  bool must_load_sco = (super_check_offset == noreg);
  if (must_load_sco) {
    assert(temp_reg != noreg, "supply either a temp or a register offset");
  } else {
    assert_different_registers(sub_klass, super_klass, super_check_offset);
  }

  Label L_fallthrough;
  int label_nulls = 0;
  if (L_success == NULL)   { L_success   = &L_fallthrough; label_nulls++; }
  if (L_failure == NULL)   { L_failure   = &L_fallthrough; label_nulls++; }
  if (L_slow_path == NULL) { L_slow_path = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one NULL in batch");

  int sc_offset  = in_bytes(Klass::secondary_super_cache_offset());
  int sco_offset = in_bytes(Klass::super_check_offset_offset());
  Address super_check_offset_addr(super_klass, sco_offset);

  // 1. If the pointers are equal, we are done (e.g., String[] elements).
  beq(sub_klass, super_klass, *L_success);

  // 2. Check the supertype display:
  if (must_load_sco) {
    lwu(temp_reg, super_check_offset_addr);
    super_check_offset = temp_reg;
  }
  add(t0, sub_klass, super_check_offset);
  ld(t0, Address(t0, 0)); // load displayed supertype

  beq(super_klass, t0, *L_success);
  mv(t1, sc_offset);
  if (L_failure == &L_fallthrough) {
    beq(super_check_offset, t1, *L_slow_path);
  } else {
    bne(super_check_offset, t1, *L_failure, /* is_far */ true);
    if (L_slow_path != &L_fallthrough) {
      j(*L_slow_path);
    }
  }

  bind(L_fallthrough);
}

void MacroAssembler::repne_scan(Register addr, Register value, Register count,
                                Register tmp) {
  Label Lloop, Lexit;
  beqz(count, Lexit);
  bind(Lloop);
  ld(tmp, Address(addr, 0));
  beq(value, tmp, Lexit);
  addi(addr, addr, wordSize);
  subi(count, count, 1);
  bnez(count, Lloop);
  bind(Lexit);
}

void MacroAssembler::check_klass_subtype_slow_path(Register sub_klass,
                                                   Register super_klass,
                                                   Register tmp1_reg,
                                                   Register tmp2_reg,
                                                   Label* L_success,
                                                   Label* L_failure) {
  assert_different_registers(sub_klass, super_klass, tmp1_reg);
  if (tmp2_reg != noreg) {
    assert_different_registers(sub_klass, super_klass, tmp1_reg, tmp2_reg, t0);
  }
#define IS_A_TEMP(reg) ((reg) == tmp1_reg || (reg) == tmp2_reg)

  Label L_fallthrough;
  int label_nulls = 0;
  if (L_success == NULL) { L_success = &L_fallthrough; label_nulls++; }
  if (L_failure == NULL) { L_failure = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one NULL in the batch");

  int ss_offset = in_bytes(Klass::secondary_supers_offset());
  int sc_offset = in_bytes(Klass::secondary_super_cache_offset());
  Address secondary_supers_addr(sub_klass, ss_offset);
  Address super_cache_addr(sub_klass, sc_offset);

  assert(sub_klass != x10, "killed reg");
  assert(sub_klass != x12, "killed reg");

  RegSet pushed_registers;
  if (!IS_A_TEMP(x12)) {
    pushed_registers += x12;
  }
  if (!IS_A_TEMP(x15)) {
    pushed_registers += x15;
  }
  if (super_klass != x10 && !IS_A_TEMP(x10)) {
    pushed_registers += x10;
  }

  push_reg(pushed_registers, sp);

  // Get super_klass value into x10 (even if it was in x15 or x12)
  mv(x10, super_klass);

#ifndef PRODUCT
  mv(t1, (address)&SharedRuntime::_partial_subtype_ctr);
  ld(t0, Address(t1, 0));
  addi(t0, t0, 1);
  sd(t0, Address(t1, 0));
#endif // PRODUCT

  // Consult the secondary-super array.
  ld(x15, secondary_supers_addr);
  lwu(x12, Address(x15, Array<Klass*>::length_offset_in_bytes()));
  addi(x15, x15, Array<Klass*>::base_offset_in_bytes());

  // Set t0 to an obvious invalid value, falling through by default
  li(t0, -1);
  repne_scan(x15, x10, x12, t0);

  // pop will restore x10, so keep its value in t1
  mv(t1, x10);

  pop_reg(pushed_registers, sp);

  bne(t1, t0, *L_failure);

  // Success. Cache the super we found and proceed.
  sd(super_klass, super_cache_addr);

  if (L_success != &L_fallthrough) {
    j(*L_success);
  }

#undef IS_A_TEMP

  bind(L_fallthrough);
}

void MacroAssembler::check_klass_subtype_slow_path_linear(Register sub_klass,
                                                          Register super_klass,
                                                          Register tmp1_reg,
                                                          Register tmp2_reg,
                                                          Label* L_success,
                                                          Label* L_failure) {
  check_klass_subtype_slow_path(sub_klass, super_klass, tmp1_reg, tmp2_reg, L_success, L_failure);
}

Address MacroAssembler::add_memory_helper(const Address dst) {
  switch (dst.getMode()) {
    case Address::base_plus_offset:
      return form_address(t1, dst.base(), dst.offset());
    default:
      la(t1, dst);
      return Address(t1, 0);
  }
}

void MacroAssembler::add_memory_int64(const Address dst, int64_t imm) {
  Address adr = add_memory_helper(dst);
  assert_different_registers(adr.base(), t0);
  ld(t0, adr);
  addi(t0, t0, imm);
  sd(t0, adr);
}

void MacroAssembler::add_memory_int32(const Address dst, int32_t imm) {
  Address adr = add_memory_helper(dst);
  assert_different_registers(adr.base(), t0);
  lwu(t0, adr);
  addiw(t0, t0, imm);
  sw(t0, adr);
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
// Allocation & Zeroing Helpers
// -------------------------------------------------------------
void MacroAssembler::tlab_allocate(Register obj,
                                   Register var_size_in_bytes,
                                   int con_size_in_bytes,
                                   Register t1,
                                   Register t2,
                                   Label& slow_case,
                                   bool is_far) {
  assert_different_registers(obj, t2);
  assert_different_registers(obj, var_size_in_bytes);
  Register end = t2;

  ld(obj, Address(xthread, JavaThread::tlab_top_offset()));
  if (var_size_in_bytes == noreg) {
    la(end, Address(obj, con_size_in_bytes));
  } else {
    add(end, obj, var_size_in_bytes);
  }
  ld(t0, Address(xthread, JavaThread::tlab_end_offset()));
  bgtu(end, t0, slow_case, is_far);

  // update the tlab top pointer
  sd(end, Address(xthread, JavaThread::tlab_top_offset()));

  if (var_size_in_bytes == end) {
    sub(var_size_in_bytes, var_size_in_bytes, obj);
  }
}

// Defines obj, preserves var_size_in_bytes
void MacroAssembler::eden_allocate(Register obj,
                                   Register var_size_in_bytes,
                                   int con_size_in_bytes,
                                   Register tmp1,
                                   Label& slow_case,
                                   bool is_far) {
  assert_different_registers(obj, var_size_in_bytes, tmp1);
  if (CMSIncrementalMode || !Universe::heap()->supports_inline_contig_alloc()) {
    j(slow_case);
  } else {
    Register end = tmp1;
    Label retry;
    bind(retry);

    // Get the current end of the heap
    ExternalAddress address_end((address) Universe::heap()->end_addr());
    {
      int32_t offset = 0;
      la_patchable(t1, address_end, offset);
      Assembler::ld(t1, t1, offset);
    }

    // Get the current top of the heap
    ExternalAddress address_top((address) Universe::heap()->top_addr());
    {
      int32_t offset = 0;
      la_patchable(t0, address_top, offset);
      addi(t0, t0, offset);
      lr_d(obj, t0, true /* aq */, true /* rl */);
    }

    // Adjust it by the size of our new object
    if (var_size_in_bytes == noreg) {
      la(end, Address(obj, con_size_in_bytes));
    } else {
      add(end, obj, var_size_in_bytes);
    }

    // if end < obj then we wrapped around high memory
    bltu(end, obj, slow_case, is_far);

    bgtu(end, t1, slow_case, is_far);

    // If heap_top hasn't been changed by some other thread, update it.
    sc_d(t1, end, t0, false /* aq */, true /* rl */);
    bnez(t1, retry);

    incr_allocated_bytes(xthread, var_size_in_bytes, con_size_in_bytes, tmp1);
  }
}

void MacroAssembler::incr_allocated_bytes(Register thread,
                                          Register var_size_in_bytes,
                                          int con_size_in_bytes,
                                          Register t1) {
  if (!thread->is_valid()) {
    thread = xthread;
  }
  assert(t1->is_valid(), "need temp reg");

  ld(t1, Address(thread, in_bytes(JavaThread::allocated_bytes_offset())));
  if (var_size_in_bytes->is_valid()) {
    add(t1, t1, var_size_in_bytes);
  } else {
    la(t1, Address(t1, con_size_in_bytes));
  }
  sd(t1, Address(thread, in_bytes(JavaThread::allocated_bytes_offset())));
}

// Zero words; len is in bytes
// Destroys all registers except addr
// len must be a nonzero multiple of wordSize
void MacroAssembler::zero_memory(Register addr, Register len, Register tmp) {
  assert_different_registers(addr, len, tmp, t0, t1);

#ifdef ASSERT
  {
    Label L;
    andi(t0, len, BytesPerWord - 1);
    beqz(t0, L);
    stop("len is not a multiple of BytesPerWord");
    bind(L);
  }
#endif // ASSERT

  Label loop;
  Label entry;

  const int unroll = 8;   // Number of sd(zr) instructions we'll unroll

  srli(len, len, LogBytesPerWord);
  andi(t0, len, unroll - 1);  // t0 = cnt % unroll
  sub(len, len, t0);          // cnt -= unroll
  // tmp always points to the end of the region we're about to zero
  shadd(tmp, t0, addr, t1, LogBytesPerWord);
  la(t1, entry);
  slli(t0, t0, 2);
  sub(t1, t1, t0);
  jr(t1);
  bind(loop);
  subi(len, len, unroll);
  for (int i = -unroll; i < 0; i++) {
    Assembler::sd(zr, tmp, i * wordSize);
  }
  bind(entry);
  addi(tmp, tmp, unroll * wordSize);
  bnez(len, loop);
}

#define SmallArraySize (18 * BytesPerLong)

// base:  Address of a buffer to be zeroed, 8 bytes aligned.
// cnt:   Immediate count in HeapWords.
void MacroAssembler::zero_words(Register base, uint64_t cnt) {
  assert_different_registers(base, t0, t1);

  if (cnt <= SmallArraySize / BytesPerLong) {
    for (int i = 0; i < (int)cnt; i++) {
      sd(zr, Address(base, i * wordSize));
    }
  } else {
    const int unroll = 8; // Number of sd(zr, adr) instructions we'll unroll
    int remainder = cnt % unroll;
    for (int i = 0; i < remainder; i++) {
      sd(zr, Address(base, i * wordSize));
    }

    Label loop;
    Register cnt_reg = t0;
    Register loop_base = t1;
    cnt = cnt - remainder;
    li(cnt_reg, cnt);
    la(loop_base, Address(base, remainder * wordSize));
    bind(loop);
    subi(cnt_reg, cnt_reg, unroll);
    for (int i = 0; i < unroll; i++) {
      Assembler::sd(zr, loop_base, i * wordSize);
    }
    addi(loop_base, loop_base, unroll * wordSize);
    bnez(cnt_reg, loop);
  }
}

// base:   Address of a buffer to be filled, 8 bytes aligned.
// cnt:    Count in 8-byte unit.
// value:  Value to be filled with.
// base will point to the end of the buffer after filling.
void MacroAssembler::fill_words(Register base, Register cnt, Register value) {
  assert_different_registers(base, cnt, value, t0, t1);

  Label fini, entry, loop;
  const int unroll = 8; // Number of sd instructions we'll unroll

  beqz(cnt, fini);

  andi(t0, cnt, unroll - 1);
  sub(cnt, cnt, t0);
  shadd(base, t0, base, t1, 3);
  la(t1, entry);
  slli(t0, t0, 2);
  sub(t1, t1, t0);
  jr(t1);

  bind(loop);
  addi(base, base, unroll * 8);
  for (int i = -unroll; i < 0; i++) {
    Assembler::sd(value, base, i * 8);
  }
  bind(entry);
  subi(cnt, cnt, unroll);
  bgez(cnt, loop);

  bind(fini);
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

void MacroAssembler::add(Register Rd, Register Rn, int64_t increment, Register tmp) {
  if (is_simm12(increment)) {
    addi(Rd, Rn, increment);
  } else {
    assert_different_registers(Rn, tmp);
    li(tmp, increment);
    add(Rd, Rn, tmp);
  }
}

void MacroAssembler::addw(Register Rd, Register Rn, int64_t increment, Register tmp) {
  if (is_simm12(increment)) {
    addiw(Rd, Rn, increment);
  } else {
    assert_different_registers(Rn, tmp);
    li(tmp, increment);
    addw(Rd, Rn, tmp);
  }
}

void MacroAssembler::sub(Register Rd, Register Rn, int64_t decrement, Register tmp) {
  int64_t val = -decrement;
  if (is_simm12(val)) {
    addi(Rd, Rn, val);
  } else {
    assert_different_registers(Rn, tmp);
    li(tmp, decrement);
    sub(Rd, Rn, tmp);
  }
}

void MacroAssembler::subw(Register Rd, Register Rn, int64_t decrement, Register tmp) {
  int64_t val = -decrement;
  if (-2048 <= val && val <= 2047) {
    addiw(Rd, Rn, val);
  } else {
    assert_different_registers(Rn, tmp);
    li(tmp, decrement);
    subw(Rd, Rn, tmp);
  }
}

void MacroAssembler::andi(Register Rd, Register Rn, int64_t imm, Register tmp) {
  if (is_simm12(imm)) {
    Assembler::andi(Rd, Rn, imm);
  } else {
    assert_different_registers(Rn, tmp);
    li(tmp, imm);
    andr(Rd, Rn, tmp);
  }
}

void MacroAssembler::orptr(Address adr, RegisterOrConstant src, Register tmp1, Register tmp2) {
  ld(tmp1, adr);
  if (src.is_register()) {
    orr(tmp1, tmp1, src.as_register());
  } else {
    if (is_simm12(src.as_constant())) {
      ori(tmp1, tmp1, src.as_constant());
    } else {
      assert_different_registers(tmp1, tmp2);
      li(tmp2, src.as_constant());
      orr(tmp1, tmp1, tmp2);
    }
  }
  sd(tmp1, adr);
}

int MacroAssembler::corrected_idivl(Register result, Register rs1, Register rs2, bool want_remainder) {
  int idivl_offset = offset();
  if (!want_remainder) {
    divw(result, rs1, rs2);
  } else {
    remw(result, rs1, rs2);
  }
  return idivl_offset;
}

int MacroAssembler::corrected_idivq(Register result, Register rs1, Register rs2, bool want_remainder) {
  int idivq_offset = offset();
  if (!want_remainder) {
    div(result, rs1, rs2);
  } else {
    rem(result, rs1, rs2);
  }
  return idivq_offset;
}

void MacroAssembler::load_reserved(Register addr, enum operand_size size, Assembler::Aqrl acquire) {
  bool aq = (acquire & Assembler::aq) != 0;
  bool rl = (acquire & Assembler::rl) != 0;
  switch (size) {
    case int64:
      lr_d(t0, addr, aq, rl);
      break;
    case int32:
      lr_w(t0, addr, aq, rl);
      break;
    case uint32:
      lr_w(t0, addr, aq, rl);
      zero_extend(t0, t0, 32);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::store_conditional(Register addr, Register new_val, enum operand_size size, Assembler::Aqrl release) {
  bool aq = (release & Assembler::aq) != 0;
  bool rl = (release & Assembler::rl) != 0;
  switch (size) {
    case int64:
      sc_d(t0, new_val, addr, aq, rl);
      break;
    case int32:
    case uint32:
      sc_w(t0, new_val, addr, aq, rl);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::cmpxchg(Register addr, Register expected,
                             Register new_val,
                             enum operand_size size,
                             Assembler::Aqrl acquire, Assembler::Aqrl release,
                             Register result, bool result_as_bool) {
  assert(size != int8 && size != int16, "unsupported operand size");

  Label retry_load, done, ne_done;
  bind(retry_load);
  load_reserved(addr, size, acquire);
  bne(t0, expected, ne_done);
  store_conditional(addr, new_val, size, release);
  bnez(t0, retry_load);

  if (result_as_bool) {
    li(result, 1);
  } else {
    mv(result, expected);
  }
  j(done);

  bind(ne_done);
  if (result_as_bool) {
    mv(result, zr);
  } else {
    mv(result, t0);
  }

  bind(done);
}

#define ATOMIC_OP(NAME, AOP, AQ, RL)                                                        \
void MacroAssembler::atomic_##NAME(Register prev, RegisterOrConstant incr, Register addr) { \
  prev = prev->is_valid() ? prev : zr;                                                      \
  if (incr.is_register()) {                                                                 \
    AOP(prev, incr.as_register(), addr, AQ, RL);                                            \
  } else {                                                                                  \
    mv(t0, incr.as_constant());                                                             \
    AOP(prev, t0, addr, AQ, RL);                                                            \
  }                                                                                         \
}

ATOMIC_OP(add,    amoadd_d, false, false)
ATOMIC_OP(addw,   amoadd_w, false, false)
ATOMIC_OP(addal,  amoadd_d, true,  true)
ATOMIC_OP(addalw, amoadd_w, true,  true)

#undef ATOMIC_OP

#define ATOMIC_XCHG(OP, AOP, AQ, RL)                                                 \
void MacroAssembler::atomic_##OP(Register prev, Register newv, Register addr) {      \
  prev = prev->is_valid() ? prev : zr;                                               \
  AOP(prev, newv, addr, AQ, RL);                                                     \
}

ATOMIC_XCHG(xchg,    amoswap_d, false, false)
ATOMIC_XCHG(xchgw,   amoswap_w, false, false)
ATOMIC_XCHG(xchgal,  amoswap_d, true,  true)
ATOMIC_XCHG(xchgalw, amoswap_w, true,  true)

#undef ATOMIC_XCHG

void MacroAssembler::atomic_xchgwu(Register prev, Register newv, Register addr) {
  atomic_xchgw(prev, newv, addr);
  zero_extend(prev, prev, 32);
}

void MacroAssembler::atomic_xchgalwu(Register prev, Register newv, Register addr) {
  atomic_xchgalw(prev, newv, addr);
  zero_extend(prev, prev, 32);
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
  mv(dst, Address((address)obj, rspec));
}

void MacroAssembler::movoop(Register dst, jobject obj, bool immediate) {
  int oop_index;
  if (obj == NULL) {
    oop_index = oop_recorder()->allocate_oop_index(obj);
  } else {
    oop_index = oop_recorder()->find_index(obj);
  }
  RelocationHolder rspec = oop_Relocation::spec(oop_index);
  if (!immediate) {
    address dummy = address(uintptr_t(pc()) & -wordSize); // A nearby aligned address
    ld_constant(dst, Address(dummy, rspec));
  } else {
    mv(dst, Address((address)obj, rspec));
  }
}

// Move a narrow oop into a register (2-instruction patchable li32: lui + addiw)
void MacroAssembler::set_narrow_oop(Register dst, jobject obj) {
  assert(UseCompressedOops, "should only be used for compressed oops");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int oop_index;
  if (obj == NULL) {
    oop_index = oop_recorder()->allocate_oop_index(obj);
  } else {
    oop_index = oop_recorder()->find_index(obj);
  }
  RelocationHolder rspec = oop_Relocation::spec(oop_index);

  InstructionMark im(this);
  code_section()->relocate(pc(), rspec);
  if (obj == NULL) {
    li32(dst, 0);
  } else {
    li32(dst, (int32_t)oopDesc::encode_heap_oop(JNIHandles::resolve(obj)));
  }
}

// Move a narrow klass pointer into a register (2-instruction patchable li32: lui + addiw)
void MacroAssembler::set_narrow_klass(Register dst, Klass* k) {
  assert(UseCompressedClassPointers, "should only be used for compressed header");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int metadata_index = oop_recorder()->find_index(k);
  RelocationHolder rspec = metadata_Relocation::spec(metadata_index);
  narrowOop encoded_k = Klass::encode_klass(k);

  InstructionMark im(this);
  code_section()->relocate(pc(), rspec);
  li32(dst, (int32_t)encoded_k);
}

Address MacroAssembler::argument_address(RegisterOrConstant arg_slot,
                                         int extra_slot_offset) {
  // cf. TemplateTable::prepare_invoke(), if (load_receiver).
  int stackElementSize = Interpreter::stackElementSize;
  int offset = Interpreter::expr_offset_in_bytes(extra_slot_offset+0);
#ifdef ASSERT
  int offset1 = Interpreter::expr_offset_in_bytes(extra_slot_offset+1);
  assert(offset1 - offset == stackElementSize, "correct arithmetic");
#endif
  if (arg_slot.is_constant()) {
    return Address(esp, arg_slot.as_constant() * stackElementSize + offset);
  } else {
    assert_different_registers(t0, arg_slot.as_register());
    shadd(t0, arg_slot.as_register(), esp, t0, exact_log2(stackElementSize));
    return Address(t0, offset);
  }
}

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
