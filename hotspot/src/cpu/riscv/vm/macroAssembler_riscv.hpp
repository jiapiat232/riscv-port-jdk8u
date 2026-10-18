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

#ifndef CPU_RISCV_VM_MACROASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_MACROASSEMBLER_RISCV_HPP

#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"

class MacroAssembler: public Assembler {
 public:
  enum {
    instruction_size = 4,
    compressed_instruction_size = 2
  };

  MacroAssembler(CodeBuffer* code) : Assembler(code) {}

  void align(int modulus) {
    while (offset() % modulus != 0) {
      nop();
    }
  }

  // -------------------------------------------------------------
  // Data Movement & Constant Loading
  // -------------------------------------------------------------
  void mov(Register dst, Register src) {
    mv(dst, src);
  }

  void movptr(Register dst, intptr_t val);
  void li(Register rd, intptr_t imm) { movptr(rd, imm); }

  // -------------------------------------------------------------
  // Memory Operations with Address
  // -------------------------------------------------------------
  void ld(Register dst, Address adr);
  void sd(Register src, Address adr);
  void lw(Register dst, Address adr);
  void sw(Register src, Address adr);
  void lh(Register dst, Address adr);
  void sh(Register src, Address adr);
  void lb(Register dst, Address adr);
  void sb(Register src, Address adr);
  void lbu(Register rd, Address adr);
  void lhu(Register rd, Address adr);
  void lwu(Register rd, Address adr);

  void fld(FloatRegister dst, Address adr);
  void fsd(FloatRegister src, Address adr);
  void flw(FloatRegister dst, Address adr);
  void fsw(FloatRegister src, Address adr);

  // -------------------------------------------------------------
  // Stack and Frame Management
  // -------------------------------------------------------------
  void build_frame(int framesize);
  void remove_frame(int framesize);
  virtual void bang_stack_with_offset(int offset);

  void push(Register r);
  void pop(Register r);
  void push(FloatRegister r);
  void pop(FloatRegister r);
  void enter();
  void leave();

  // -------------------------------------------------------------
  // Calls and Jumps
  // -------------------------------------------------------------
  void call(address entry);
  void jmp(address entry);
  void far_jump(address entry) { jmp(entry); }

  // -------------------------------------------------------------
  // Branch and Jump with Labels
  // -------------------------------------------------------------
  using Assembler::beq;
  using Assembler::bne;
  using Assembler::blt;
  using Assembler::bge;
  using Assembler::bltu;
  using Assembler::bgeu;
  using Assembler::jal;
  using Assembler::jalr;

  void beq(Register rs1, Register rs2, Label& L);
  void bne(Register rs1, Register rs2, Label& L);
  void blt(Register rs1, Register rs2, Label& L);
  void bge(Register rs1, Register rs2, Label& L);
  void bltu(Register rs1, Register rs2, Label& L);
  void bgeu(Register rs1, Register rs2, Label& L);

  void beqz(Register rs, Label& L) { beq(rs, zr, L); }
  void bnez(Register rs, Label& L) { bne(rs, zr, L); }
  void blez(Register rs, Label& L) { bge(zr, rs, L); }
  void bgez(Register rs, Label& L) { bge(rs, zr, L); }
  void bltz(Register rs, Label& L) { blt(rs, zr, L); }
  void bgtz(Register rs, Label& L) { blt(zr, rs, L); }

  void j(Label& L);
  void jal(Register rd, Label& L);

  static void pd_patch_instruction(address branch, address target);

  // -------------------------------------------------------------
  // Safepoints, GC and Thread State
  // -------------------------------------------------------------
  void safepoint_poll(Register tmp);
  void reinit_heapbase();
  void load_byte_map_base(Register dst);
  void get_thread(Register dst) {
    mv(dst, xthread);
  }

  void set_last_Java_frame(Register last_java_sp, Register last_java_fp, address last_java_pc, Register tmp);
  void reset_last_Java_frame(bool clear_fp = true);

  // -------------------------------------------------------------
  // Runtime Calls (call_VM family)
  // -------------------------------------------------------------
  void call_VM(Register oop_result, address entry_point, bool check_exceptions = true);
  void call_VM(Register oop_result, address entry_point, Register arg_1, bool check_exceptions = true);
  void call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, bool check_exceptions = true);
  void call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions = true);

  void call_VM(Register oop_result, Register last_java_sp, address entry_point, int number_of_arguments = 0, bool check_exceptions = true);
  void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, bool check_exceptions = true);
  void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, bool check_exceptions = true);
  void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool check_exceptions = true);

  void call_VM_leaf(address entry_point, int number_of_arguments = 0);
  void call_VM_leaf(address entry_point, Register arg_1);
  void call_VM_leaf(address entry_point, Register arg_1, Register arg_2);
  void call_VM_leaf(address entry_point, Register arg_1, Register arg_2, Register arg_3);

  // -------------------------------------------------------------
  // Verification and Debugging
  // -------------------------------------------------------------
  void verify_oop(Register reg, const char* s = "bad oop") PRODUCT_RETURN;
  void verify_oop_addr(Address addr, const char* s = "bad oop") PRODUCT_RETURN;

  virtual void null_check(Register reg, int offset = -1);
  static bool needs_explicit_null_check(intptr_t offset);
  static bool uses_implicit_null_check(void* address);

  void stop(const char* msg) {
    ebreak();
  }
  void warn(const char* msg) {}
  void untested() { stop("untested"); }
  void unimplemented(const char* what = "") { stop(what); }
  void should_not_reach_here() { stop("should not reach here"); }
};

#endif // CPU_RISCV_VM_MACROASSEMBLER_RISCV_HPP
