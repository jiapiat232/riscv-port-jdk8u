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

  void align(int modulus, int extra_offset = 0) {
    while ((offset() + extra_offset) % modulus != 0) {
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

  // Fixed-length 6-instruction (24-byte) patchable pointer load:
  //   movptr_with_offset (5 insns: lui + addi + slli(11) + addi + slli(6)) + addi
  void movptr_with_offset(Register Rd, address addr, int32_t &offset);
  void movptr(Register dst, intptr_t val);

  // Variable-length (1..6 insns) unpatchable immediate load:
  void li(Register rd, intptr_t imm);
  void mv(Register rd, int64_t imm) { li(rd, imm); }
  void mv(Register rd, address addr) { movptr(rd, (intptr_t)addr); }
  void mv(Register rd, Address dest);
  void mv(Register rd, RegisterOrConstant src) {
    if (src.is_register()) {
      mv(rd, src.as_register());
    } else {
      li(rd, src.as_constant());
    }
  }

  void mov_metadata(Register dst, Metadata* obj);
  void movoop(Register dst, jobject obj, bool immediate = true);
  void set_narrow_oop(Register dst, jobject obj);
  void set_narrow_klass(Register dst, Klass* k);

  // -------------------------------------------------------------
  // Memory Operations with Address
  // -------------------------------------------------------------
  using Assembler::ld;
  using Assembler::sd;
  using Assembler::lw;
  using Assembler::sw;
  using Assembler::lh;
  using Assembler::sh;
  using Assembler::lb;
  using Assembler::sb;
  using Assembler::lbu;
  using Assembler::lhu;
  using Assembler::lwu;
  using Assembler::fld;
  using Assembler::fsd;
  using Assembler::flw;
  using Assembler::fsw;

  void ld(Register dst, Address adr, Register tmp = t0);
  void ld(Register dst, Label& L);
  void sd(Register src, Address adr, Register tmp = t0);
  void lw(Register dst, Address adr, Register tmp = t0);
  void sw(Register src, Address adr, Register tmp = t0);
  void lh(Register dst, Address adr, Register tmp = t0);
  void sh(Register src, Address adr, Register tmp = t0);
  void lb(Register dst, Address adr, Register tmp = t0);
  void sb(Register src, Address adr, Register tmp = t0);
  void lbu(Register rd, Address adr, Register tmp = t0);
  void lhu(Register rd, Address adr, Register tmp = t0);
  void lwu(Register rd, Address adr, Register tmp = t0);

  void fld(FloatRegister dst, Address adr, Register tmp = t0);
  void fsd(FloatRegister src, Address adr, Register tmp = t0);
  void flw(FloatRegister dst, Address adr, Register tmp = t0);
  void fsw(FloatRegister src, Address adr, Register tmp = t0);

  // -------------------------------------------------------------
  // Stack and Frame Management
  // -------------------------------------------------------------
  void build_frame(int framesize);
  void remove_frame(int framesize);
  int load_unsigned_byte(Register dst, Address src)  { int off = offset(); lbu(dst, src); return off; }
  int load_signed_byte(Register dst, Address src)    { int off = offset(); lb(dst, src);  return off; }
  int load_unsigned_short(Register dst, Address src) { int off = offset(); lhu(dst, src); return off; }
  int load_signed_short(Register dst, Address src)   { int off = offset(); lh(dst, src);  return off; }
  void load_sized_value(Register dst, Address src, size_t size_in_bytes, bool is_signed);

  virtual void bang_stack_with_offset(int offset);
  void bang_stack_size(Register size, Register tmp);

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
  void push_call_clobbered_registers_except(RegSet exclude);
  void pop_call_clobbered_registers_except(RegSet exclude);
  void push_call_clobbered_registers() { push_call_clobbered_registers_except(RegSet()); }
  void pop_call_clobbered_registers() { pop_call_clobbered_registers_except(RegSet()); }
  void push_CPU_state();
  void pop_CPU_state();
  void enter();
  void leave();

  // -------------------------------------------------------------
  // Calls and Jumps
  // -------------------------------------------------------------
  static bool far_branches() {
    return ReservedCodeCacheSize > branch_range;
  }

  static int far_branch_size() {
    return far_branches() ? (2 * instruction_size) : instruction_size;
  }

  void call(address entry);
  void jmp(address entry);
  void far_jump(address entry) { jmp(entry); }
  void far_jump(const Address& entry, CodeBuffer* cbuf = NULL, Register tmp = t0);
  void far_jump(const Address& entry, Register tmp) { far_jump(entry, NULL, tmp); }
  void far_call(const Address& entry, CodeBuffer* cbuf = NULL, Register tmp = t0);
  void far_call(const Address& entry, Register tmp) { far_call(entry, NULL, tmp); }
  void la_patchable(Register reg1, const Address& dest, int32_t& offset);

  address trampoline_call(Address entry, CodeBuffer* cbuf = NULL);
  address ic_call(address entry, jint method_index = 0);
  address emit_trampoline_stub(int insts_call_instruction_offset, address target);
  void emit_static_call_stub();
  static int static_call_stub_size();

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

  void beq(Register rs1, Register rs2, Label& L, bool is_far = false);
  void bne(Register rs1, Register rs2, Label& L, bool is_far = false);
  void blt(Register rs1, Register rs2, Label& L, bool is_far = false);
  void bge(Register rs1, Register rs2, Label& L, bool is_far = false);
  void bltu(Register rs1, Register rs2, Label& L, bool is_far = false);
  void bgeu(Register rs1, Register rs2, Label& L, bool is_far = false);

  void bgt(Register rs1, Register rs2, Label& L, bool is_far = false)  { blt(rs2, rs1, L, is_far); }
  void ble(Register rs1, Register rs2, Label& L, bool is_far = false)  { bge(rs2, rs1, L, is_far); }
  void bgtu(Register rs1, Register rs2, Label& L, bool is_far = false) { bltu(rs2, rs1, L, is_far); }
  void bleu(Register rs1, Register rs2, Label& L, bool is_far = false) { bgeu(rs2, rs1, L, is_far); }

  void beqz(Register rs, Label& L, bool is_far = false) { beq(rs, zr, L, is_far); }
  void bnez(Register rs, Label& L, bool is_far = false) { bne(rs, zr, L, is_far); }
  void blez(Register rs, Label& L, bool is_far = false) { bge(zr, rs, L, is_far); }
  void bgez(Register rs, Label& L, bool is_far = false) { bge(rs, zr, L, is_far); }
  void bltz(Register rs, Label& L, bool is_far = false) { blt(rs, zr, L, is_far); }
  void bgtz(Register rs, Label& L, bool is_far = false) { blt(zr, rs, L, is_far); }

  // Floating-point compare and branch
  void float_beq(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void float_bne(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void float_ble(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void float_blt(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void float_bge(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void float_bgt(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);

  void double_beq(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void double_bne(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void double_ble(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void double_blt(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void double_bge(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);
  void double_bgt(FloatRegister Rs1, FloatRegister Rs2, Label& L, bool is_far = false, bool is_unordered = false);

  void j(Label& L);
  void j(const Address& entry, Register tmp = t0) { far_jump(entry, tmp); }
  void j(address entry, Register tmp = t0)        { far_jump(entry); }
  void jal(Register rd, Label& L);

  static address target_addr_for_insn(address insn_addr);
  static int pd_patch_instruction_size(address branch, address target);
  static void pd_patch_instruction(address branch, address target) {
    pd_patch_instruction_size(branch, target);
  }
  static address pd_call_destination(address branch) {
    return target_addr_for_insn(branch);
  }
  static int patch_oop(address insn_addr, address o);

  // -------------------------------------------------------------
  // Safepoints, GC and Thread State
  // -------------------------------------------------------------
  void safepoint_poll(Register tmp);
  void get_polling_page(Register dest, address page, relocInfo::relocType rtype);
  void get_polling_page(Register dest, relocInfo::relocType rtype) {
    get_polling_page(dest, os::get_polling_page(), rtype);
  }
  address read_polling_page(Register r, address page, relocInfo::relocType rtype);
  address read_polling_page(Register r, relocInfo::relocType rtype);
  address read_polling_page(Register r, int32_t offset, relocInfo::relocType rtype);
  void reinit_heapbase();
  void load_byte_map_base(Register dst);
  void get_thread(Register dst) {
    mv(dst, xthread);
  }

  void test_bit(Register Rd, Register Rs, uint32_t bit_pos);

  // Rd = (Rs1 << shamt) + Rs2
  void shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt);
  void shift_left_add(Register Rd, Register Rs1, Register Rs2, int shamt, Register tmp);
  void shadd(Register Rd, Register Rs1, Register Rs2, Register tmp, int shamt) {
    shift_left_add(Rd, Rs1, Rs2, shamt, tmp);
  }
  Address form_address(Register Rd, Register base, long byte_offset);

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

  static void membar_mask_to_pred_succ(uint32_t order_constraint, uint32_t& predecessor, uint32_t& successor) {
    predecessor = (order_constraint >> 2) & 0x3;
    successor = order_constraint & 0x3;
    if (UseConservativeFence) {
      predecessor |= predecessor << 2;
      successor |= successor << 2;
    }
  }

  static int pred_succ_to_membar_mask(uint32_t predecessor, uint32_t successor) {
    return ((predecessor & 0x3) << 2) | (successor & 0x3);
  }

  static bool is_valid_riscv64_address(address addr) {
    return ((uintptr_t)addr >> 47) == 0;
  }

  void ld_constant(Register dest, const Address &const_addr) {
    int32_t offset = 0;
    la_patchable(dest, InternalAddress(const_addr.target()), offset);
    Assembler::ld(dest, dest, offset);
  }

  void zext(Register dst, Register src, int bits);
  void sext(Register dst, Register src, int bits);
  void zero_extend(Register dst, Register src, int bits) { zext(dst, src, bits); }
  void sign_extend(Register dst, Register src, int bits) { sext(dst, src, bits); }
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
  void encode_klass_not_null(Register r);
  void encode_klass_not_null(Register dst, Register src, Register tmp = t0);
  void decode_klass_not_null(Register r);
  void decode_klass_not_null(Register dst, Register src, Register tmp = t0);
  void load_klass(Register dst, Register src);
  void store_klass(Register dst, Register src);
  void store_klass_gap(Register dst, Register src);
  void load_prototype_header(Register dst, Register src);
  void cmp_klass(Register oop, Register trial_klass, Register tmp, Label &L);

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
                                     Label* L_slow_path,
                                     Register super_check_offset = noreg);
  void check_klass_subtype_slow_path(Register sub_klass,
                                     Register super_klass,
                                     Register tmp1_reg,
                                     Register tmp2_reg,
                                     Label* L_success,
                                     Label* L_failure);
  void check_klass_subtype_slow_path_linear(Register sub_klass,
                                            Register super_klass,
                                            Register tmp1_reg,
                                            Register tmp2_reg,
                                            Label* L_success,
                                            Label* L_failure);
  void repne_scan(Register addr, Register value, Register count, Register tmp);

  Address add_memory_helper(const Address dst);
  void add_memory_int64(const Address dst, int64_t imm);
  void add_memory_int32(const Address dst, int32_t imm);

  // Method lookup
  Address argument_address(RegisterOrConstant arg_slot, int extra_slot_offset = 0);
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

  // Allocation & Zeroing
  void tlab_allocate(Register obj,
                     Register var_size_in_bytes,
                     int con_size_in_bytes,
                     Register t1,
                     Register t2,
                     Label& slow_case,
                     bool is_far = false);
  void eden_allocate(Register obj,
                     Register var_size_in_bytes,
                     int con_size_in_bytes,
                     Register t1,
                     Label& slow_case,
                     bool is_far = false);
  void incr_allocated_bytes(Register thread,
                            Register var_size_in_bytes,
                            int con_size_in_bytes,
                            Register t1);
  void zero_memory(Register addr, Register len, Register tmp);
  void zero_words(Register base, uint64_t cnt);
  void fill_words(Register base, Register cnt, Register value);

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
  void revb_h_h(Register Rd, Register Rs, Register tmp = t0);
  void revb_w_w(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void revb_h_h_u(Register Rd, Register Rs, Register tmp = t0);
  void revb_h_w_u(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void revb_h_helper(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void revb_h(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void revb_w(Register Rd, Register Rs, Register tmp1 = t0, Register tmp2 = t1);
  void ror_imm(Register dst, Register src, uint32_t shift, Register tmp = t0);

  void fcvt_w_s_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_l_s_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_w_d_safe(Register dst, FloatRegister src, Register tmp = t0);
  void fcvt_l_d_safe(Register dst, FloatRegister src, Register tmp = t0);

  inline void mvw(Register Rd, int32_t imm32) { mv(Rd, (int64_t)imm32); }

  using Assembler::add;
  using Assembler::addw;
  using Assembler::sub;
  using Assembler::subw;
  void add(Register Rd, Register Rn, int64_t increment, Register tmp = t0);
  void addw(Register Rd, Register Rn, int64_t increment, Register tmp = t0);
  void sub(Register Rd, Register Rn, int64_t decrement, Register tmp = t0);
  void subw(Register Rd, Register Rn, int64_t decrement, Register tmp = t0);
  void andi(Register Rd, Register Rn, int64_t imm, Register tmp = t0);
  void orptr(Address adr, RegisterOrConstant src, Register tmp1 = t0, Register tmp2 = t1);

  int corrected_idivl(Register result, Register rs1, Register rs2, bool want_remainder);
  int corrected_idivq(Register result, Register rs1, Register rs2, bool want_remainder);

  void load_reserved(Register addr, enum operand_size size, Assembler::Aqrl acquire);
  void store_conditional(Register addr, Register new_val, enum operand_size size, Assembler::Aqrl release);
  void cmpxchg(Register addr, Register expected,
               Register new_val,
               enum operand_size size,
               Assembler::Aqrl acquire, Assembler::Aqrl release,
               Register result, bool result_as_bool = false);

  void atomic_add(Register prev, RegisterOrConstant incr, Register addr);
  void atomic_addw(Register prev, RegisterOrConstant incr, Register addr);
  void atomic_addal(Register prev, RegisterOrConstant incr, Register addr);
  void atomic_addalw(Register prev, RegisterOrConstant incr, Register addr);

  void atomic_xchg(Register prev, Register newv, Register addr);
  void atomic_xchgw(Register prev, Register newv, Register addr);
  void atomic_xchgal(Register prev, Register newv, Register addr);
  void atomic_xchgalw(Register prev, Register newv, Register addr);
  void atomic_xchgwu(Register prev, Register newv, Register addr);
  void atomic_xchgalwu(Register prev, Register newv, Register addr);

  static const int zero_words_block_size;
  address zero_words(Register ptr, Register cnt);

#ifdef COMPILER2
  // -------------------------------------------------------------
  // C2 MacroAssembler Primitives (Bridges JDK 8 ADLC _masm & JDK 19 enc_class)
  // -------------------------------------------------------------
  static const int bool_test_bits = 3;
  static const int neg_cond_bits = 2;
  static const int unsigned_branch_mask = 1 << bool_test_bits;
  static const int double_branch_mask = 1 << bool_test_bits;

  void cmp_branch(int cmpFlag,
                  Register op1, Register op2,
                  Label& label, bool is_far = false);

  void float_cmp_branch(int cmpFlag,
                        FloatRegister op1, FloatRegister op2,
                        Label& label, bool is_far = false);

  void enc_cmpUEqNeLeGt_imm0_branch(int cmpFlag, Register op,
                                    Label& L, bool is_far = false);

  void enc_cmpEqNe_imm0_branch(int cmpFlag, Register op,
                               Label& L, bool is_far = false);

  void enc_cmove(int cmpFlag,
                 Register op1, Register op2,
                 Register dst, Register src);

  void spill(Register r, bool is64, int offset) {
    is64 ? sd(r, Address(sp, offset))
         : sw(r, Address(sp, offset));
  }

  void spill(FloatRegister f, bool is64, int offset) {
    is64 ? fsd(f, Address(sp, offset))
         : fsw(f, Address(sp, offset));
  }

  void unspill(Register r, bool is64, int offset) {
    is64 ? ld(r, Address(sp, offset))
         : lw(r, Address(sp, offset));
  }

  void unspillu(Register r, bool is64, int offset) {
    is64 ? ld(r, Address(sp, offset))
         : lwu(r, Address(sp, offset));
  }

  void unspill(FloatRegister f, bool is64, int offset) {
    is64 ? fld(f, Address(sp, offset))
         : flw(f, Address(sp, offset));
  }

  void minmax_FD(FloatRegister dst,
                 FloatRegister src1, FloatRegister src2,
                 bool is_double, bool is_min);

  void mul_add(Register out, Register in, Register offset,
               Register len, Register k, Register tmp);
  void cad(Register dst, Register src1, Register src2, Register carry);
  void cadc(Register dst, Register src1, Register src2, Register carry);
  void adc(Register dst, Register src1, Register src2, Register carry);
  void add2_with_carry(Register final_dest_hi, Register dest_hi, Register dest_lo,
                       Register src1, Register src2, Register carry);
  void multiply_32_x_32_loop(Register x, Register xstart, Register x_xstart,
                             Register y, Register y_idx, Register z,
                             Register carry, Register product,
                             Register idx, Register kdx);
  void multiply_64_x_64_loop(Register x, Register xstart, Register x_xstart,
                             Register y, Register y_idx, Register z,
                             Register carry, Register product,
                             Register idx, Register kdx);
  void multiply_128_x_128_loop(Register y, Register z,
                               Register carry, Register carry2,
                               Register idx, Register jdx,
                               Register yz_idx1, Register yz_idx2,
                               Register tmp, Register tmp3, Register tmp4,
                               Register tmp6, Register product_hi);
  void multiply_to_len(Register x, Register xlen, Register y, Register ylen,
                       Register z, Register zlen,
                       Register tmp1, Register tmp2, Register tmp3, Register tmp4,
                       Register tmp5, Register tmp6, Register product_hi);
#endif // COMPILER2

  virtual void _call_Unimplemented(address call_site) {
    mv(t1, call_site);
    stop("Unimplemented");
  }
  #define call_Unimplemented() _call_Unimplemented((address)__PRETTY_FUNCTION__)

  // -------------------------------------------------------------
  // Verification and Debugging
  // -------------------------------------------------------------
  void _verify_method_ptr(Register reg, const char * msg, const char * file, int line) {}
  void _verify_klass_ptr(Register reg, const char * msg, const char * file, int line) {}
#define verify_method_ptr(reg) _verify_method_ptr(reg, "broken method " #reg, __FILE__, __LINE__)
#define verify_klass_ptr(reg) _verify_klass_ptr(reg, "broken klass " #reg, __FILE__, __LINE__)

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

typedef MacroAssembler C2_MacroAssembler;

#endif // CPU_RISCV_VM_MACROASSEMBLER_RISCV_HPP
