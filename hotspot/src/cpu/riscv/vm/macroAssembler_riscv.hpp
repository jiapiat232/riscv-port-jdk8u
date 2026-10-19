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

class Method;
class BiasedLockingCounters;

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
  using Assembler::mv;
  void mov(Register dst, Register src) {
    Assembler::mv(dst, src);
  }

  void movptr_with_offset(Register Rd, address addr, int32_t &offset);
  void movptr(Register dst, intptr_t val);
  void li(Register rd, intptr_t imm) { movptr(rd, imm); }
  void mv(Register rd, int64_t imm) { li(rd, imm); }
  void mv(Register rd, address addr) { movptr(rd, (intptr_t)addr); }
  void mov_metadata(Register dst, Metadata* obj);
  void movoop(Register dst, jobject obj);

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
  void load_unsigned_byte(Register dst, Address src) { lbu(dst, src); }
  void load_signed_byte(Register dst, Address src)   { lb(dst, src); }
  void load_unsigned_short(Register dst, Address src){ lhu(dst, src); }
  void load_signed_short(Register dst, Address src)  { lh(dst, src); }
  void load_sized_value(Register dst, Address src, size_t size_in_bytes, bool is_signed);

  virtual void bang_stack_with_offset(int offset);

  void push(Register r);
  void pop(Register r);
  void push(FloatRegister r);
  void pop(FloatRegister r);
  void push_reg(Register r);
  void pop_reg(Register r);
  int  bitset_to_regs(unsigned int bitset, unsigned char* regs);
  int  push_reg(unsigned int bitset, Register stack);
  int  pop_reg(unsigned int bitset, Register stack);
  void push_reg(RegSet regs, Register stack) { if (regs.bits()) { push_reg(regs.bits(), stack); } }
  void pop_reg(RegSet regs, Register stack) { if (regs.bits()) { pop_reg(regs.bits(), stack); } }
  void enter();
  void leave();

  // -------------------------------------------------------------
  // Calls and Jumps
  // -------------------------------------------------------------
  void call(address entry);
  void jmp(address entry);
  void far_jump(address entry) { jmp(entry); }
  void far_jump(const Address& entry, Register tmp = t0);
  void far_call(const Address& entry, Register tmp = t0);

  void la(Register Rd, const Address& adr);
  void la(Register Rd, const address addr);
  void la(Register Rd, Label& L);

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

  void bgt(Register rs1, Register rs2, Label& L)  { blt(rs2, rs1, L); }
  void ble(Register rs1, Register rs2, Label& L)  { bge(rs2, rs1, L); }
  void bgtu(Register rs1, Register rs2, Label& L) { bltu(rs2, rs1, L); }
  void bleu(Register rs1, Register rs2, Label& L) { bgeu(rs2, rs1, L); }

  void beqz(Register rs, Label& L) { beq(rs, zr, L); }
  void bnez(Register rs, Label& L) { bne(rs, zr, L); }
  void blez(Register rs, Label& L) { bge(zr, rs, L); }
  void bgez(Register rs, Label& L) { bge(rs, zr, L); }
  void bltz(Register rs, Label& L) { blt(rs, zr, L); }
  void bgtz(Register rs, Label& L) { blt(zr, rs, L); }

  void j(Label& L);
  void j(const Address& entry, Register tmp = t0) { far_jump(entry, tmp); }
  void j(address entry, Register tmp = t0)        { far_jump(entry); }
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

  void test_bit(Register Rd, Register Rs, uint32_t bit_pos);

  // Rd = (Rs1 << shamt) + Rs2
  void shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt);
  void shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt, Register tmp);

  void andrw(Register rd, Register rs1, Register rs2) { andr(rd, rs1, rs2); addiw(rd, rd, 0); }
  void orrw(Register rd, Register rs1, Register rs2)  { orr(rd, rs1, rs2); addiw(rd, rd, 0); }
  void xorrw(Register rd, Register rs1, Register rs2) { xorr(rd, rs1, rs2); addiw(rd, rd, 0); }

  // Memory Ordering Barriers
  enum Membar_mask_bits {
    StoreStore = 0b0101,               // (pred = w   + succ = w)
    LoadStore  = 0b1001,               // (pred = r   + succ = w)
    StoreLoad  = 0b0110,               // (pred = w   + succ = r)
    LoadLoad   = 0b1010,               // (pred = r   + succ = r)
    AnyAny     = LoadStore | StoreLoad // (pred = rw  + succ = rw)
  };

  void membar(uint32_t order_constraint);

  void zext(Register dst, Register src, int bits);
  void sext(Register dst, Register src, int bits);
  void c2bool(Register Rd, Register Rs) { sltu(Rd, zr, Rs); }
  void c2bool(Register Rd) { c2bool(Rd, Rd); }
  void cast_primitive_type(BasicType type, Register Rt);
  void serialize_memory(Register thread, Register tmp) { membar(AnyAny); }

  // Oop encoding and GC barriers
  void encode_heap_oop(Register d, Register s);
  void encode_heap_oop(Register r) { encode_heap_oop(r, r); }
  void decode_heap_oop(Register d, Register s);
  void decode_heap_oop(Register r) { decode_heap_oop(r, r); }
  void encode_heap_oop_not_null(Register d, Register s);
  void encode_heap_oop_not_null(Register r) { encode_heap_oop_not_null(r, r); }
  void decode_heap_oop_not_null(Register d, Register s);
  void decode_heap_oop_not_null(Register r) { decode_heap_oop_not_null(r, r); }

  void load_heap_oop(Register dst, Address src);
  void load_heap_oop_not_null(Register dst, Address src);
  void store_heap_oop(Address dst, Register src);
  void store_heap_oop_null(Address dst);
  void store_check(Register obj);

  void round_to(Register reg, int modulus) {
    addi(reg, reg, modulus - 1);
    andi(reg, reg, -modulus);
  }

  // Klass encoding/decoding and loading/storing
  void encode_klass_not_null(Register r, Register tmp = t0);
  void encode_klass_not_null(Register dst, Register src, Register tmp = t0);
  void decode_klass_not_null(Register r, Register tmp = t0);
  void decode_klass_not_null(Register dst, Register src, Register tmp = t0);
  void load_klass(Register dst, Register src);
  void store_klass(Register dst, Register src);
  void store_klass_gap(Register dst, Register src);
  void load_prototype_header(Register dst, Register src);

  // Atomic CAS and Biased Locking support (JDK 8u architecture + RISC-V LR/SC)
  void cmpxchgptr(Register oldv, Register newv, Register addr, Register tmp,
                  Label &succeed, Label *fail);
  void cmpxchg_obj_header(Register oldv, Register newv, Register obj, Register tmp,
                          Label &succeed, Label *fail);
  int biased_locking_enter(Register lock_reg, Register obj_reg,
                           Register swap_reg, Register tmp_reg,
                           bool swap_reg_contains_mark,
                           Label& done, Label* slow_case = NULL,
                           BiasedLockingCounters* counters = NULL);
  void biased_locking_exit(Register obj_reg, Register temp_reg, Label& done);

  // Subtype check
  void check_klass_subtype(Register sub_klass,
                           Register super_klass,
                           Register temp_reg,
                           Label& L_success);
  void check_klass_subtype_fast_path(Register sub_klass,
                                     Register super_klass,
                                     Register temp_reg,
                                     Label* L_success,
                                     Label* L_failure,
                                     Label* L_slow_path);
  void check_klass_subtype_slow_path_linear(Register sub_klass,
                                            Register super_klass,
                                            Register tmp1_reg,
                                            Register tmp2_reg,
                                            Label* L_success,
                                            Label* L_failure);

  // Method lookup
  void lookup_virtual_method(Register recv_klass,
                             RegisterOrConstant vtable_index,
                             Register method_result);
  void lookup_interface_method(Register recv_klass,
                               Register intf_klass,
                               RegisterOrConstant itable_index,
                               Register method_result,
                               Register scan_temp,
                               Label& L_no_such_interface,
                               bool return_method = true);

  // Allocation
  void tlab_allocate(Register obj,
                     Register var_size_in_bytes,
                     int con_size_in_bytes,
                     Register t1,
                     Register t2,
                     Label& slow_case);

#if INCLUDE_ALL_GCS
  void g1_write_barrier_pre(Register obj,
                            Register pre_val,
                            Register thread,
                            Register tmp,
                            bool tosca_live,
                            bool expand_call);
  void g1_write_barrier_post(Register store_addr,
                             Register new_val,
                             Register thread,
                             Register tmp,
                             Register tmp2);
#endif

  void set_last_Java_frame(Register last_java_sp, Register last_java_fp, address last_java_pc, Register tmp);
  void set_last_Java_frame(Register last_java_sp, Register last_java_fp, Label& L, Register tmp);
  void reset_last_Java_frame(bool clear_fp = true);

  virtual void call_VM_leaf_base(address entry_point,
                                 int     number_of_arguments = 0,
                                 Label  *retaddr = NULL);
  void call_VM_leaf_base(address entry_point,
                         int     number_of_arguments,
                         Label  &retaddr) {
    call_VM_leaf_base(entry_point, number_of_arguments, &retaddr);
  }

  virtual void call_VM_base(Register oop_result,
                            Register java_thread,
                            Register last_java_sp,
                            address  entry_point,
                            int      number_of_arguments = 0,
                            bool     check_exceptions = true);
  virtual void check_and_handle_earlyret(Register java_thread) {}
  virtual void check_and_handle_popframe(Register java_thread) {}

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

  void get_vm_result  (Register oop_result, Register java_thread);
  void get_vm_result_2(Register metadata_result, Register java_thread);

  // Comparisons
  void cmp_x2i(Register dst, Register src1, Register src2, Register tmp = t0, bool is_signed = true);
  void cmp_l2i(Register dst, Register src1, Register src2, Register tmp = t0);
  void float_compare(Register result, FloatRegister Rs1, FloatRegister Rs2, int unordered_result);
  void double_compare(Register result, FloatRegister Rs1, FloatRegister Rs2, int unordered_result);

  // Extension & Conversion Helpers
  void narrow_subword_type(Register reg, BasicType bt);
  void revbw(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void revb(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);

  void fcvt_w_s_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_l_s_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_w_d_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_l_d_safe(Register dst, FloatRegister src, Register tmp = t0);

  using Assembler::subw;
  void subw(Register Rd, Register Rn, int64_t decrement, Register tmp = t0);

  // -------------------------------------------------------------
  // Verification and Debugging
  // -------------------------------------------------------------
  void _verify_method_ptr(Register reg, const char * msg, const char * file, int line) {}
#define verify_method_ptr(reg) _verify_method_ptr(reg, "broken method " #reg, __FILE__, __LINE__)

  void verify_oop(Register reg, const char* s = "bad oop") PRODUCT_RETURN;
  void verify_oop_addr(Address addr, const char* s = "bad oop") PRODUCT_RETURN;

  virtual void null_check(Register reg, int offset = -1);
  static bool needs_explicit_null_check(intptr_t offset);
  static bool uses_implicit_null_check(void* address);

  static void debug64(char* msg, address bcp, Method* m = NULL, intptr_t dispatched_bc = -1);
  void stop(const char* msg);
  void warn(const char* msg) {}
  void untested() { stop("untested"); }
  void unimplemented(const char* what = "") { stop(what); }
  void should_not_reach_here() { stop("should not reach here"); }
};

#endif // CPU_RISCV_VM_MACROASSEMBLER_RISCV_HPP
