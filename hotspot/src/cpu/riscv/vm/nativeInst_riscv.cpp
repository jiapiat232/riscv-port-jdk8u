/*
 * Copyright (c) 1997, 2020, Oracle and/or its affiliates. All rights reserved.
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
#include "asm/macroAssembler.hpp"
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/ostream.hpp"
#ifdef COMPILER1
#include "c1/c1_Runtime1.hpp"
#endif

//-------------------------------------------------------------------

void NativeInstruction::print() {
  tty->print_cr(INTPTR_FORMAT ": 0x%08x", p2i(addr_at(0)), uint_at(0));
}

bool NativeInstruction::is_lwu_to_zr(address instr) {
  // lwu zr, imm(rs1): opcode = 0b0000011 (0x03), funct3 = 0b110 (0x6), rd = x0 (0)
  return (*(uint32_t*)instr & 0x7fff) == 0x6003;
}

bool NativeInstruction::is_safepoint_poll() {
  // Safepoint poll is implemented via lwu(zr, Address(reg, 0))
  return is_lwu_to_zr(addr_at(0));
}

bool NativeInstruction::is_sigill_zombie_not_entrant() {
  return uint_at(0) == NativeIllegalInstruction::instruction_code;
}

bool NativeInstruction::is_stop() {
  return false;
}

Register NativeInstruction::extract_rs1(address instr) {
  return as_Register(Assembler::extract(*(uint32_t*)instr, 19, 15));
}

Register NativeInstruction::extract_rs2(address instr) {
  return as_Register(Assembler::extract(*(uint32_t*)instr, 24, 20));
}

Register NativeInstruction::extract_rd(address instr) {
  return as_Register(Assembler::extract(*(uint32_t*)instr, 11, 7));
}

uint32_t NativeInstruction::extract_opcode(address instr) {
  return Assembler::extract(*(uint32_t*)instr, 6, 0);
}

uint32_t NativeInstruction::extract_funct3(address instr) {
  return Assembler::extract(*(uint32_t*)instr, 14, 12);
}

// First instruction: auipc.
// Second instruction: addi, jalr, ld, lw, lwu, flw, or fld.
bool NativeInstruction::is_pc_relative_at(address branch) {
  if (!is_auipc_at(branch)) {
    return false;
  }
  address insn2 = branch + instruction_size;
  uint32_t opcode2 = extract_opcode(insn2);
  uint32_t funct3  = extract_funct3(insn2);
  Register rd1     = extract_rd(branch);
  Register rs1_2   = extract_rs1(insn2);

  if (rd1 != rs1_2) {
    return false;
  }

  switch (opcode2) {
    case 0b0010011: // OP-IMM (addi)
    case 0b1100111: // JALR
      return funct3 == 0b000;
    case 0b0000011: // LOAD (lb, lh, lw, ld, lbu, lhu, lwu)
      return true;
    case 0b0000111: // LOAD-FP (flw, fld)
      return funct3 == 0b010 || funct3 == 0b011;
    default:
      return false;
  }
}

bool NativeInstruction::is_load_pc_relative_at(address branch) {
  return is_auipc_at(branch) &&
         is_ld_at(branch + instruction_size) &&
         extract_rd(branch) == extract_rs1(branch + instruction_size) &&
         extract_rd(branch) == extract_rd(branch + instruction_size);
}

// Check 6-instruction movptr sequence:
//   0: lui   rd, upper
//   4: addi  rd, rd, lower
//   8: slli  rd, rd, 11
//  12: addi  rd, rd, imm11
//  16: slli  rd, rd, 6
//  20: addi/jalr/load rd/zr/ra, rd, imm6
bool NativeInstruction::is_movptr_at(address branch) {
  address i0 = branch;
  address i1 = branch + instruction_size;
  address i2 = branch + instruction_size * 2;
  address i3 = branch + instruction_size * 3;
  address i4 = branch + instruction_size * 4;
  address i5 = branch + instruction_size * 5;

  if (extract_opcode(i0) != 0b0110111) return false; // lui
  if (extract_opcode(i1) != 0b0010011 || extract_funct3(i1) != 0b000) return false; // addi
  if (extract_opcode(i2) != 0b0010011 || extract_funct3(i2) != 0b001 ||
      Assembler::extract(*(uint32_t*)i2, 31, 20) != 11) return false; // slli 11
  if (extract_opcode(i3) != 0b0010011 || extract_funct3(i3) != 0b000) return false; // addi
  if (extract_opcode(i4) != 0b0010011 || extract_funct3(i4) != 0b001 ||
      Assembler::extract(*(uint32_t*)i4, 31, 20) != 6) return false;  // slli 6

  uint32_t op5 = extract_opcode(i5);
  uint32_t f5  = extract_funct3(i5);
  bool valid_i5 = (op5 == 0b0010011 && f5 == 0b000) || // addi
                  (op5 == 0b1100111 && f5 == 0b000) || // jalr
                  (op5 == 0b0000011)                || // any load (lb/lh/lw/ld/lbu/lhu/lwu)
                  (op5 == 0b0100011)                || // any store (sb/sh/sw/sd)
                  (op5 == 0b0000111 && (f5 == 0b010 || f5 == 0b011)) || // flw/fld
                  (op5 == 0b0100111 && (f5 == 0b010 || f5 == 0b011));   // fsw/fsd
  if (!valid_i5) return false;

  Register rd = extract_rd(i0);
  return extract_rs1(i1) == rd && extract_rd(i1) == rd &&
         extract_rs1(i2) == rd && extract_rd(i2) == rd &&
         extract_rs1(i3) == rd && extract_rd(i3) == rd &&
         extract_rs1(i4) == rd && extract_rd(i4) == rd &&
         extract_rs1(i5) == rd;
}

// Check 2-instruction li32 sequence:
//   0: lui   rd, upper
//   4: addiw rd, rd, lower
bool NativeInstruction::is_li32_at(address branch) {
  address i0 = branch;
  address i1 = branch + instruction_size;
  return extract_opcode(i0) == 0b0110111 && // lui
         extract_opcode(i1) == 0b0011011 && // addiw (OP-IMM-32)
         extract_funct3(i1) == 0b000     &&
         extract_rd(i0) == extract_rs1(i1) &&
         extract_rd(i0) == extract_rd(i1);
}

//-------------------------------------------------------------------

void NativeCall::verify() {
  assert(NativeCall::is_call_at((address)this), "unexpected code at call site");
}

address NativeCall::destination() const {
  address addr = (address)this;
  address destination = MacroAssembler::target_addr_for_insn(addr);

  // Do we use a trampoline stub for this call?
  // Trampoline stubs are located behind the main code.
  if (destination > addr) {
    // Filter out recursive method invocation (call to verified/unverified entry point).
    CodeBlob* cb = CodeCache::find_blob_unsafe(addr);   // Else we get assertion if nmethod is zombie.
    assert(cb && cb->is_nmethod(), "sanity");
    nmethod *nm = (nmethod *)cb;
    if (nm->stub_contains(destination) && is_NativeCallTrampolineStub_at(destination)) {
      // Yes we do, so get the destination from the trampoline stub.
      const address trampoline_stub_addr = destination;
      destination = nativeCallTrampolineStub_at(trampoline_stub_addr)->destination();
    }
  }

  return destination;
}

// Similar to replace_mt_safe, but just changes the destination. The
// important thing is that free-running threads are able to execute this
// call instruction at all times.
//
// Used in the runtime linkage of calls; see class CompiledIC.
//
// Why does this work on RISC-V?
// The call instruction is a single 32-bit aligned jal word, so a 32-bit store
// updates it atomically with respect to instruction fetch after icache flush.
void NativeCall::set_destination_mt_safe(address dest, bool assert_lock) {
  debug_only(verify());
  if (assert_lock) {
    assert(Patching_lock->is_locked() ||
           SafepointSynchronize::is_at_safepoint(),
           "concurrent code patching");
  }

  address addr_call = addr_at(0);
  assert(NativeCall::is_call_at(addr_call), "unexpected code at call site");

  // Patch the constant in the call's trampoline stub if needed.
  address trampoline_stub_addr = get_trampoline();
  if (trampoline_stub_addr != NULL) {
    assert(!is_NativeCallTrampolineStub_at(dest), "chained trampolines");
    nativeCallTrampolineStub_at(trampoline_stub_addr)->set_destination(dest);
  }

  // Patch the call.
  if (Assembler::reachable_from_branch_at(addr_call, dest)) {
    set_destination(dest);
  } else {
    assert(trampoline_stub_addr != NULL, "we need a trampoline");
    set_destination(trampoline_stub_addr);
  }

  ICache::invalidate_range(addr_call, instruction_size);
}

address NativeCall::get_trampoline() {
  address call_addr = addr_at(0);

  CodeBlob *code = CodeCache::find_blob(call_addr);
  assert(code != NULL, "Could not find the containing code blob");

  address jal_destination = MacroAssembler::target_addr_for_insn(call_addr);
  if (code->content_contains(jal_destination) &&
      is_NativeCallTrampolineStub_at(jal_destination)) {
    return jal_destination;
  }

  // If the codeBlob is not a nmethod, this is because we get here from the
  // CodeBlob constructor, which is called within the nmethod constructor.
  return trampoline_stub_Relocation::get_trampoline_for(call_addr, (nmethod*)code);
}

// If the instructions are copied and was not relocated yet, generate a trampoline jump.
address NativeCall::trampoline_jump(CodeBuffer &cbuf, address dest) {
  address call_addr = addr_at(0);
  address stub = NULL;

  if (cbuf.insts()->contains(call_addr) &&
      !Assembler::reachable_from_branch_at(call_addr, dest)) {
    // Check if we already generated a trampoline stub for this call.
    RelocIterator iter(cbuf.stubs());
    while (iter.next()) {
      if (iter.type() == relocInfo::trampoline_stub_type) {
        if (iter.trampoline_stub_reloc()->owner() == call_addr) {
          stub = iter.addr();
          break;
        }
      }
    }

    if (stub != NULL) {
      nativeCallTrampolineStub_at(stub)->set_destination(dest);
      set_destination(stub);
    }
  }

  return stub;
}

void NativeCall::insert(address code_pos, address entry) {
  Unimplemented();
}

void NativeCall::replace_mt_safe(address instr_addr, address code_buffer) {
  Unimplemented();
}

void NativeCall::print() {
  tty->print_cr(INTPTR_FORMAT ": call " INTPTR_FORMAT,
                p2i(instruction_address()), p2i(destination()));
}

//-------------------------------------------------------------------

void NativeMovConstReg::verify() {
  if (!(NativeInstruction::is_movptr_at(instruction_address()) ||
        NativeInstruction::is_load_pc_relative_at(instruction_address()) ||
        NativeInstruction::is_li32_at(instruction_address()))) {
    fatal("not a movptr, load_pc_relative, or li32 instruction in NativeMovConstReg");
  }
}

intptr_t NativeMovConstReg::data() const {
  address addr = MacroAssembler::target_addr_for_insn(instruction_address());
  if (maybe_cpool_ref(instruction_address())) {
    return *(intptr_t*)addr;
  } else {
    return (intptr_t)addr;
  }
}

void NativeMovConstReg::set_data(intptr_t x) {
  if (maybe_cpool_ref(instruction_address())) {
    address addr = MacroAssembler::target_addr_for_insn(instruction_address());
    *(intptr_t*)addr = x;
  } else {
    // Store x into the instruction stream.
    MacroAssembler::pd_patch_instruction_size(instruction_address(), (address)x);
    ICache::invalidate_range(instruction_address(), movptr_instruction_size);
  }

  // Find and update the oop/metadata corresponding to this instruction in oops section.
  CodeBlob* cb = CodeCache::find_blob(instruction_address());
  nmethod* nm = cb ? cb->as_nmethod_or_null() : NULL;
  if (nm != NULL) {
    RelocIterator iter(nm, instruction_address(), next_instruction_address());
    while (iter.next()) {
      if (iter.type() == relocInfo::oop_type) {
        oop* oop_addr = iter.oop_reloc()->oop_addr();
        *oop_addr = cast_to_oop(x);
        break;
      } else if (iter.type() == relocInfo::metadata_type) {
        Metadata** metadata_addr = iter.metadata_reloc()->metadata_addr();
        *metadata_addr = (Metadata*)x;
        break;
      }
    }
  }
}

void NativeMovConstReg::print() {
  tty->print_cr(INTPTR_FORMAT ": mov reg, " INTPTR_FORMAT,
                p2i(instruction_address()), data());
}

//-------------------------------------------------------------------

int NativeMovRegMem::offset() const  {
  return (int)(intptr_t)MacroAssembler::target_addr_for_insn(instruction_address());
}

void NativeMovRegMem::set_offset(int x) {
  MacroAssembler::pd_patch_instruction_size(instruction_address(), (address)(intptr_t)x);
  ICache::invalidate_range(instruction_address(), instruction_size);
}

void NativeMovRegMem::verify() {
  assert(NativeInstruction::is_movptr_at(instruction_address()), "not a movptr reg, offset");
}

void NativeMovRegMem::print() {
  tty->print_cr(INTPTR_FORMAT ": mov reg, [reg + %x]", p2i(instruction_address()), offset());
}

//-------------------------------------------------------------------

void NativeJump::verify() {
  assert(NativeInstruction::is_jal_at(instruction_address()) ||
         NativeInstruction::is_pc_relative_at(instruction_address()) ||
         NativeInstruction::is_movptr_at(instruction_address()), "not a jump instruction");
}

address NativeJump::jump_destination() const {
  address dest = MacroAssembler::target_addr_for_insn(instruction_address());

  // We use jump to self as the unresolved address which the inline
  // cache code (and relocs) know about.
  // As a special case we also use sequence movptr_with_offset(r,0), jalr(r,0)
  // i.e. jump to 0 when we need to leave space for a wide immediate load.
  if ((dest == (address)this) || dest == 0) {
    dest = (address)-1;
  }
  return dest;
}

void NativeJump::set_jump_destination(address dest) {
  // We use jump to self as the unresolved address which the inline
  // cache code (and relocs) know about
  if (dest == (address)-1)
    dest = instruction_address();

  int size = MacroAssembler::pd_patch_instruction_size(instruction_address(), dest);
  ICache::invalidate_range(instruction_address(), size);
}

void NativeJump::insert(address code_pos, address entry) {
  Unimplemented();
}

void NativeJump::check_verified_entry_alignment(address entry, address verified_entry) {
  // Nothing to do on RISC-V (4-byte aligned instructions).
}

// MT-safe patching of a verified method entry at runtime (e.g. nmethod::make_not_entrant_or_zombie).
// If dest is within +/- 1MB jal range, writes a 4-byte jal instruction atomically.
// Otherwise, writes NativeIllegalInstruction::instruction_code (0xffffffff), which triggers SIGILL
// and is caught by JVM_handle_linux_signal -> SharedRuntime::get_handle_wrong_method_stub().
void NativeJump::patch_verified_entry(address entry, address verified_entry, address dest) {
  assert(dest == SharedRuntime::get_handle_wrong_method_stub(), "expected fixeddestination of patch");
  assert( verified_entry != NULL, "sanity" );

  if (Assembler::reachable_from_branch_at(verified_entry, dest)) {
    ptrdiff_t offset = dest - verified_entry;
    uint32_t insn = 0b1101111; // jal zr, offset
    address pInsn = (address)&insn;
    Assembler::patch(pInsn, 31, 31, (offset >> 20) & 0x1);
    Assembler::patch(pInsn, 30, 21, (offset >> 1) & 0x3ff);
    Assembler::patch(pInsn, 20, 20, (offset >> 11) & 0x1);
    Assembler::patch(pInsn, 19, 12, (offset >> 12) & 0xff);
    Assembler::patch_reg(pInsn, 11, 7, zr);
    *(uint32_t*)verified_entry = insn;
  } else {
    // Signal handler in os_linux_riscv.cpp catches SIGILL on 0xffffffff
    // (is_sigill_zombie_not_entrant) and jumps to SharedRuntime::get_handle_wrong_method_stub().
    *(uint32_t*)verified_entry = NativeIllegalInstruction::instruction_code;
  }

  ICache::invalidate_range(verified_entry, instruction_size);
}

//-------------------------------------------------------------------

address NativeGeneralJump::jump_destination() const {
  NativeMovConstReg* move = nativeMovConstReg_at(instruction_address());
  return (address)move->data();
}

void NativeGeneralJump::insert_unconditional(address code_pos, address entry) {
  NativeGeneralJump* n_jump = (NativeGeneralJump*)code_pos;

  CodeBuffer cb(code_pos, instruction_size);
  MacroAssembler a(&cb);

  int32_t offset = 0;
  a.movptr_with_offset(t0, entry, offset);
  a.jalr(zr, t0, offset);

  ICache::invalidate_range(code_pos, instruction_size);
}

// MT-safe patching of a general jump instruction (used by C1 PatchingStub).
// In C1 PatchingStub, the jump instruction emitted initially is a 4-byte jal zr, offset (j(Label)).
void NativeGeneralJump::replace_mt_safe(address instr_addr, address code_buffer) {
  assert(NativeInstruction::is_jal_at(instr_addr) ||
         nativeInstruction_at(instr_addr)->is_nop(), "must be jal or nop at C1 patching point");
  uint32_t load_instr = *(uint32_t*)code_buffer;
  *(uint32_t*)instr_addr = load_instr;
  ICache::invalidate_range(instr_addr, NativeInstruction::instruction_size);
}

void NativeGeneralJump::verify() {
  assert(NativeInstruction::is_movptr_at(instruction_address()) ||
         NativeInstruction::is_jal_at(instruction_address()) ||
         NativeInstruction::is_pc_relative_at(instruction_address()), "invalid NativeGeneralJump");
}

//-------------------------------------------------------------------

void NativeIllegalInstruction::insert(address code_pos) {
  *(juint*)code_pos = instruction_code;
  ICache::invalidate_range(code_pos, instruction_size);
}

//-------------------------------------------------------------------

address NativeCallTrampolineStub::destination(nmethod *nm) const {
  return ptr_at(data_offset);
}

void NativeCallTrampolineStub::set_destination(address new_destination) {
  set_ptr_at(data_offset, new_destination);
  OrderAccess::release();
}

ptrdiff_t NativeCallTrampolineStub::destination_offset() const {
  return data_offset;
}

//-------------------------------------------------------------------

uint32_t NativeMembar::get_kind() {
  uint32_t insn = uint_at(0);
  uint32_t pred = Assembler::extract(insn, 27, 24);
  uint32_t succ = Assembler::extract(insn, 23, 20);
  return MacroAssembler::pred_succ_to_membar_mask(pred, succ);
}

void NativeMembar::set_kind(uint32_t order_kind) {
  uint32_t pred = 0;
  uint32_t succ = 0;
  MacroAssembler::membar_mask_to_pred_succ(order_kind, pred, succ);
  Assembler::patch(addr_at(0), 27, 24, pred);
  Assembler::patch(addr_at(0), 23, 20, succ);
}
