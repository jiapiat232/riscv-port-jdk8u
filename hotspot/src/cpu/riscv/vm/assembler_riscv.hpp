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

#ifndef CPU_RISCV_VM_ASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_ASSEMBLER_RISCV_HPP

#include "asm/register.hpp"

class Argument VALUE_OBJ_CLASS_SPEC {
 public:
  enum {
    n_int_register_parameters_c   = 8,  // x10..x17 (c_rarg0..c_rarg7)
    n_float_register_parameters_c = 8,  // f10..f17 (c_farg0..c_farg7)

    n_int_register_parameters_j   = 8,
    n_float_register_parameters_j = 8
  };
};

class Address VALUE_OBJ_CLASS_SPEC {
 private:
  Register _base;
  int32_t  _offset;

 public:
  Address() : _base(noreg), _offset(0) {}
  Address(Register base, int32_t offset = 0) : _base(base), _offset(offset) {}
  Address(Register base, ByteSize disp) : _base(base), _offset(in_bytes(disp)) {}

  Register base()   const { return _base; }
  int32_t  offset() const { return _offset; }

  bool is_simm12() const {
    return -2048 <= _offset && _offset <= 2047;
  }
};

class Assembler : public AbstractAssembler {
 public:
  // Immediate validation predicates
  static inline bool is_simm12(intptr_t x) {
    return -2048 <= x && x <= 2047;
  }
  static inline bool is_uimm12(intptr_t x) {
    return 0 <= x && x <= 4095;
  }
  static inline bool is_simm13(intptr_t x) {
    return -4096 <= x && x <= 4095 && ((x & 1) == 0);
  }
  static inline bool is_simm21(intptr_t x) {
    return -1048576 <= x && x <= 1048575 && ((x & 1) == 0);
  }
  static inline bool is_simm32(intptr_t x) {
    return (int32_t)x == x;
  }

  // Opcodes according to RISC-V Unprivileged ISA Specification
  enum Opcode {
    LOAD      = 0x03,
    LOAD_FP   = 0x07,
    MISC_MEM  = 0x0f,
    OP_IMM    = 0x13,
    AUIPC     = 0x17,
    OP_IMM_32 = 0x1b,
    STORE     = 0x23,
    STORE_FP  = 0x27,
    AMO       = 0x2f,
    OP        = 0x33,
    LUI       = 0x37,
    OP_32     = 0x3b,
    BRANCH    = 0x63,
    JALR      = 0x67,
    JAL       = 0x6f,
    SYSTEM    = 0x73,
    OP_FP     = 0x53
  };

  Assembler(CodeBuffer* code) : AbstractAssembler(code) {}

  // Emit 32-bit instruction
  void emit_int32(int32_t x) {
    AbstractAssembler::emit_int32(x);
  }

  // Bitfield instruction encoders
  static inline int32_t encode_r(int funct7, Register rs2, Register rs1, int funct3, Register rd, int opcode) {
    return ((funct7 & 0x7f) << 25) |
           ((rs2->encoding() & 0x1f) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_i(int32_t imm, Register rs1, int funct3, Register rd, int opcode) {
    assert(is_simm12(imm), "immediate out of range for I-type");
    return ((imm & 0xfff) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_s(int32_t imm, Register rs2, Register rs1, int funct3, int opcode) {
    assert(is_simm12(imm), "immediate out of range for S-type");
    return (((imm >> 5) & 0x7f) << 25) |
           ((rs2->encoding() & 0x1f) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((imm & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_b(int32_t imm, Register rs2, Register rs1, int funct3, int opcode) {
    assert(is_simm13(imm), "offset out of range for B-type");
    return (((imm >> 12) & 0x1) << 31) |
           (((imm >> 5) & 0x3f) << 25) |
           ((rs2->encoding() & 0x1f) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           (((imm >> 1) & 0xf) << 8) |
           (((imm >> 11) & 0x1) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_u(int32_t imm, Register rd, int opcode) {
    return (imm & 0xfffff000) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_j(int32_t imm, Register rd, int opcode) {
    assert(is_simm21(imm), "offset out of range for J-type");
    return (((imm >> 20) & 0x1) << 31) |
           (((imm >> 1) & 0x3ff) << 21) |
           (((imm >> 11) & 0x1) << 20) |
           (((imm >> 12) & 0xff) << 12) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  // Floating-point encoders
  static inline int32_t encode_r_fp(int funct7, FloatRegister rs2, FloatRegister rs1, int funct3, FloatRegister rd, int opcode) {
    return ((funct7 & 0x7f) << 25) |
           ((rs2->encoding() & 0x1f) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_i_fp(int32_t imm, Register rs1, int funct3, FloatRegister rd, int opcode) {
    assert(is_simm12(imm), "immediate out of range for FP I-type");
    return ((imm & 0xfff) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd->encoding() & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  static inline int32_t encode_s_fp(int32_t imm, FloatRegister rs2, Register rs1, int funct3, int opcode) {
    assert(is_simm12(imm), "immediate out of range for FP S-type");
    return (((imm >> 5) & 0x7f) << 25) |
           ((rs2->encoding() & 0x1f) << 20) |
           ((rs1->encoding() & 0x1f) << 15) |
           ((funct3 & 0x7) << 12) |
           ((imm & 0x1f) << 7) |
           (opcode & 0x7f);
  }

  // -------------------------------------------------------------
  // RV64I Integer Computational Instructions (R-Type)
  // -------------------------------------------------------------
  void add(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000000, rs2, rs1, 0b000, rd, OP)); }
  void sub(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0100000, rs2, rs1, 0b000, rd, OP)); }
  void sll(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000000, rs2, rs1, 0b001, rd, OP)); }
  void slt(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000000, rs2, rs1, 0b010, rd, OP)); }
  void sltu(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b011, rd, OP)); }
  void xorr(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b100, rd, OP)); }
  void srl(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000000, rs2, rs1, 0b101, rd, OP)); }
  void sra(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0100000, rs2, rs1, 0b101, rd, OP)); }
  void orr(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000000, rs2, rs1, 0b110, rd, OP)); }
  void andr(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b111, rd, OP)); }

  // 32-bit Word variants in RV64
  void addw(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b000, rd, OP_32)); }
  void subw(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0100000, rs2, rs1, 0b000, rd, OP_32)); }
  void sllw(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b001, rd, OP_32)); }
  void srlw(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000000, rs2, rs1, 0b101, rd, OP_32)); }
  void sraw(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0100000, rs2, rs1, 0b101, rd, OP_32)); }

  // -------------------------------------------------------------
  // RV64I Integer Computational Instructions (I-Type)
  // -------------------------------------------------------------
  void addi(Register rd, Register rs1, int32_t imm)  { emit_int32(encode_i(imm, rs1, 0b000, rd, OP_IMM)); }
  void slti(Register rd, Register rs1, int32_t imm)  { emit_int32(encode_i(imm, rs1, 0b010, rd, OP_IMM)); }
  void sltiu(Register rd, Register rs1, int32_t imm) { emit_int32(encode_i(imm, rs1, 0b011, rd, OP_IMM)); }
  void xori(Register rd, Register rs1, int32_t imm)  { emit_int32(encode_i(imm, rs1, 0b100, rd, OP_IMM)); }
  void ori(Register rd, Register rs1, int32_t imm)   { emit_int32(encode_i(imm, rs1, 0b110, rd, OP_IMM)); }
  void andi(Register rd, Register rs1, int32_t imm)  { emit_int32(encode_i(imm, rs1, 0b111, rd, OP_IMM)); }

  void slli(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x3f) == 0, "shift amount out of range for 64-bit slli");
    emit_int32(((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b001 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM);
  }
  void srli(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x3f) == 0, "shift amount out of range for 64-bit srli");
    emit_int32(((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM);
  }
  void srai(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x3f) == 0, "shift amount out of range for 64-bit srai");
    emit_int32((0b010000 << 26) | ((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM);
  }

  // 32-bit Word variants (I-Type)
  void addiw(Register rd, Register rs1, int32_t imm) { emit_int32(encode_i(imm, rs1, 0b000, rd, OP_IMM_32)); }
  void slliw(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x1f) == 0, "shift amount out of range for slliw");
    emit_int32(((shamt & 0x1f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b001 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM_32);
  }
  void srliw(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x1f) == 0, "shift amount out of range for srliw");
    emit_int32(((shamt & 0x1f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM_32);
  }
  void sraiw(Register rd, Register rs1, uint32_t shamt) {
    assert((shamt & ~0x1f) == 0, "shift amount out of range for sraiw");
    emit_int32((0b0100000 << 25) | ((shamt & 0x1f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM_32);
  }

  // -------------------------------------------------------------
  // RV64I Load and Store Instructions
  // -------------------------------------------------------------
  void lb(Register rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i(offset, rs1, 0b000, rd, LOAD)); }
  void lh(Register rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i(offset, rs1, 0b001, rd, LOAD)); }
  void lw(Register rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i(offset, rs1, 0b010, rd, LOAD)); }
  void ld(Register rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i(offset, rs1, 0b011, rd, LOAD)); }
  void lbu(Register rd, Register rs1, int32_t offset = 0) { emit_int32(encode_i(offset, rs1, 0b100, rd, LOAD)); }
  void lhu(Register rd, Register rs1, int32_t offset = 0) { emit_int32(encode_i(offset, rs1, 0b101, rd, LOAD)); }
  void lwu(Register rd, Register rs1, int32_t offset = 0) { emit_int32(encode_i(offset, rs1, 0b110, rd, LOAD)); }

  void sb(Register rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s(offset, rs2, rs1, 0b000, STORE)); }
  void sh(Register rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s(offset, rs2, rs1, 0b001, STORE)); }
  void sw(Register rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s(offset, rs2, rs1, 0b010, STORE)); }
  void sd(Register rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s(offset, rs2, rs1, 0b011, STORE)); }

  // -------------------------------------------------------------
  // RV64I Branches and Jumps
  // -------------------------------------------------------------
  void beq(Register rs1, Register rs2, int32_t offset)  { emit_int32(encode_b(offset, rs2, rs1, 0b000, BRANCH)); }
  void bne(Register rs1, Register rs2, int32_t offset)  { emit_int32(encode_b(offset, rs2, rs1, 0b001, BRANCH)); }
  void blt(Register rs1, Register rs2, int32_t offset)  { emit_int32(encode_b(offset, rs2, rs1, 0b100, BRANCH)); }
  void bge(Register rs1, Register rs2, int32_t offset)  { emit_int32(encode_b(offset, rs2, rs1, 0b101, BRANCH)); }
  void bltu(Register rs1, Register rs2, int32_t offset) { emit_int32(encode_b(offset, rs2, rs1, 0b110, BRANCH)); }
  void bgeu(Register rs1, Register rs2, int32_t offset) { emit_int32(encode_b(offset, rs2, rs1, 0b111, BRANCH)); }

  void lui(Register rd, int32_t imm20)   { emit_int32(encode_u(imm20, rd, LUI)); }
  void auipc(Register rd, int32_t imm20) { emit_int32(encode_u(imm20, rd, AUIPC)); }
  void jal(Register rd, int32_t offset)  { emit_int32(encode_j(offset, rd, JAL)); }
  void jalr(Register rd, Register rs1, int32_t offset = 0) { emit_int32(encode_i(offset, rs1, 0b000, rd, JALR)); }

  // -------------------------------------------------------------
  // RV64M Standard Extension (Multiply and Divide)
  // -------------------------------------------------------------
  void mul(Register rd, Register rs1, Register rs2)    { emit_int32(encode_r(0b0000001, rs2, rs1, 0b000, rd, OP)); }
  void mulh(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b001, rd, OP)); }
  void mulhsu(Register rd, Register rs1, Register rs2) { emit_int32(encode_r(0b0000001, rs2, rs1, 0b010, rd, OP)); }
  void mulhu(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000001, rs2, rs1, 0b011, rd, OP)); }
  void div(Register rd, Register rs1, Register rs2)    { emit_int32(encode_r(0b0000001, rs2, rs1, 0b100, rd, OP)); }
  void divu(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b101, rd, OP)); }
  void rem(Register rd, Register rs1, Register rs2)    { emit_int32(encode_r(0b0000001, rs2, rs1, 0b110, rd, OP)); }
  void remu(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b111, rd, OP)); }

  void mulw(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b000, rd, OP_32)); }
  void divw(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b100, rd, OP_32)); }
  void divuw(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000001, rs2, rs1, 0b101, rd, OP_32)); }
  void remw(Register rd, Register rs1, Register rs2)   { emit_int32(encode_r(0b0000001, rs2, rs1, 0b110, rd, OP_32)); }
  void remuw(Register rd, Register rs1, Register rs2)  { emit_int32(encode_r(0b0000001, rs2, rs1, 0b111, rd, OP_32)); }

  // -------------------------------------------------------------
  // RV64A Standard Extension (Atomic Operations)
  // -------------------------------------------------------------
  void lr_w(Register rd, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00010 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, zr, rs1, 0b010, rd, AMO));
  }
  void sc_w(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00011 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b010, rd, AMO));
  }
  void amoadd_w(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00000 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b010, rd, AMO));
  }
  void amoswap_w(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00001 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b010, rd, AMO));
  }

  void lr_d(Register rd, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00010 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, zr, rs1, 0b011, rd, AMO));
  }
  void sc_d(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00011 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b011, rd, AMO));
  }
  void amoadd_d(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00000 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b011, rd, AMO));
  }
  void amoswap_d(Register rd, Register rs2, Register rs1, bool aq = false, bool rl = false) {
    int funct7 = (0b00001 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
    emit_int32(encode_r(funct7, rs2, rs1, 0b011, rd, AMO));
  }

  // -------------------------------------------------------------
  // System and Memory Ordering Instructions
  // -------------------------------------------------------------
  void fence(uint32_t pred = 0xf, uint32_t succ = 0xf) {
    emit_int32(((pred & 0xf) << 24) | ((succ & 0xf) << 20) | (0b000 << 12) | MISC_MEM);
  }
  void fence_i() {
    emit_int32((0b001 << 12) | MISC_MEM);
  }
  void ecall()  { emit_int32((0b000 << 12) | SYSTEM); }
  void ebreak() { emit_int32((1 << 20) | (0b000 << 12) | SYSTEM); }

  // -------------------------------------------------------------
  // RV64F and RV64D Floating-Point Standard Extension
  // -------------------------------------------------------------
  void flw(FloatRegister rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i_fp(offset, rs1, 0b010, rd, LOAD_FP)); }
  void fld(FloatRegister rd, Register rs1, int32_t offset = 0)  { emit_int32(encode_i_fp(offset, rs1, 0b011, rd, LOAD_FP)); }
  void fsw(FloatRegister rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s_fp(offset, rs2, rs1, 0b010, STORE_FP)); }
  void fsd(FloatRegister rs2, Register rs1, int32_t offset = 0) { emit_int32(encode_s_fp(offset, rs2, rs1, 0b011, STORE_FP)); }

  void fadd_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0000000, rs2, rs1, 0b111, rd, OP_FP)); }
  void fsub_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0000100, rs2, rs1, 0b111, rd, OP_FP)); }
  void fmul_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0001000, rs2, rs1, 0b111, rd, OP_FP)); }
  void fdiv_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0001100, rs2, rs1, 0b111, rd, OP_FP)); }

  void fadd_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0000001, rs2, rs1, 0b111, rd, OP_FP)); }
  void fsub_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0000101, rs2, rs1, 0b111, rd, OP_FP)); }
  void fmul_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0001001, rs2, rs1, 0b111, rd, OP_FP)); }
  void fdiv_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) { emit_int32(encode_r_fp(0b0001101, rs2, rs1, 0b111, rd, OP_FP)); }

  // -------------------------------------------------------------
  // Standard Pseudo Instructions
  // -------------------------------------------------------------
  void nop()                           { addi(zr, zr, 0); }
  void mv(Register rd, Register rs)    { addi(rd, rs, 0); }
  void notr(Register rd, Register rs)  { xori(rd, rs, -1); }
  void neg(Register rd, Register rs)   { sub(rd, zr, rs); }
  void negw(Register rd, Register rs)  { subw(rd, zr, rs); }
  void j(int32_t offset)               { jal(zr, offset); }
  void jr(Register rs)                 { jalr(zr, rs, 0); }
  void jalr(Register rs)               { jalr(ra, rs, 0); }
  void ret()                           { jalr(zr, ra, 0); }

  virtual RegisterOrConstant delayed_value_impl(intptr_t* delayed_value_addr,
                                                Register tmp,
                                                int offset) {
    ShouldNotCallThis();
    return RegisterOrConstant();
  }

  virtual void bang_stack_with_offset(int offset);
};

#endif // CPU_RISCV_VM_ASSEMBLER_RISCV_HPP
