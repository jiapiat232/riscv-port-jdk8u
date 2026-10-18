/*
 * Standalone RISC-V Assembler Differential TDD Verification Tool
 * Bit-for-bit differential comparison between HotSpot bitfield encoding and GNU as.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

class RegisterImpl {
 private:
  int _encoding;
 public:
  RegisterImpl(int enc) : _encoding(enc) {}
  int encoding() const { return _encoding; }
};
typedef RegisterImpl* Register;

static RegisterImpl reg_objs[32] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

static const Register zr  = &reg_objs[0];
static const Register ra  = &reg_objs[1];
static const Register sp  = &reg_objs[2];
static const Register gp  = &reg_objs[3];
static const Register tp  = &reg_objs[4];
static const Register t0  = &reg_objs[5];
static const Register t1  = &reg_objs[6];
static const Register t2  = &reg_objs[7];
static const Register s0  = &reg_objs[8];
static const Register a0  = &reg_objs[10];
static const Register a1  = &reg_objs[11];
static const Register a2  = &reg_objs[12];
static const Register a3  = &reg_objs[13];
static const Register a4  = &reg_objs[14];
static const Register a5  = &reg_objs[15];
static const Register a6  = &reg_objs[16];
static const Register a7  = &reg_objs[17];

typedef Register FloatRegister;
static const FloatRegister fa0 = &reg_objs[10];
static const FloatRegister fa1 = &reg_objs[11];
static const FloatRegister fa2 = &reg_objs[12];

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

static inline int32_t encode_r(int funct7, Register rs2, Register rs1, int funct3, Register rd, int opcode) {
  return ((funct7 & 0x7f) << 25) |
         ((rs2->encoding() & 0x1f) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((rd->encoding() & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_i(int32_t imm, Register rs1, int funct3, Register rd, int opcode) {
  return ((imm & 0xfff) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((rd->encoding() & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_s(int32_t imm, Register rs2, Register rs1, int funct3, int opcode) {
  return (((imm >> 5) & 0x7f) << 25) |
         ((rs2->encoding() & 0x1f) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((imm & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_b(int32_t imm, Register rs2, Register rs1, int funct3, int opcode) {
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
  return (((imm >> 20) & 0x1) << 31) |
         (((imm >> 1) & 0x3ff) << 21) |
         (((imm >> 11) & 0x1) << 20) |
         (((imm >> 12) & 0xff) << 12) |
         ((rd->encoding() & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_r_fp(int funct7, FloatRegister rs2, FloatRegister rs1, int funct3, FloatRegister rd, int opcode) {
  return ((funct7 & 0x7f) << 25) |
         ((rs2->encoding() & 0x1f) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((rd->encoding() & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_i_fp(int32_t imm, Register rs1, int funct3, FloatRegister rd, int opcode) {
  return ((imm & 0xfff) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((rd->encoding() & 0x1f) << 7) |
         (opcode & 0x7f);
}

static inline int32_t encode_s_fp(int32_t imm, FloatRegister rs2, Register rs1, int funct3, int opcode) {
  return (((imm >> 5) & 0x7f) << 25) |
         ((rs2->encoding() & 0x1f) << 20) |
         ((rs1->encoding() & 0x1f) << 15) |
         ((funct3 & 0x7) << 12) |
         ((imm & 0x1f) << 7) |
         (opcode & 0x7f);
}

// Instruction emitters
uint32_t insn_add(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b000, rd, OP); }
uint32_t insn_sub(Register rd, Register rs1, Register rs2)  { return encode_r(0b0100000, rs2, rs1, 0b000, rd, OP); }
uint32_t insn_sll(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b001, rd, OP); }
uint32_t insn_slt(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b010, rd, OP); }
uint32_t insn_sltu(Register rd, Register rs1, Register rs2) { return encode_r(0b0000000, rs2, rs1, 0b011, rd, OP); }
uint32_t insn_xor(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b100, rd, OP); }
uint32_t insn_srl(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b101, rd, OP); }
uint32_t insn_sra(Register rd, Register rs1, Register rs2)  { return encode_r(0b0100000, rs2, rs1, 0b101, rd, OP); }
uint32_t insn_or(Register rd, Register rs1, Register rs2)   { return encode_r(0b0000000, rs2, rs1, 0b110, rd, OP); }
uint32_t insn_and(Register rd, Register rs1, Register rs2)  { return encode_r(0b0000000, rs2, rs1, 0b111, rd, OP); }

uint32_t insn_addw(Register rd, Register rs1, Register rs2) { return encode_r(0b0000000, rs2, rs1, 0b000, rd, OP_32); }
uint32_t insn_subw(Register rd, Register rs1, Register rs2) { return encode_r(0b0100000, rs2, rs1, 0b000, rd, OP_32); }
uint32_t insn_sllw(Register rd, Register rs1, Register rs2) { return encode_r(0b0000000, rs2, rs1, 0b001, rd, OP_32); }
uint32_t insn_srlw(Register rd, Register rs1, Register rs2) { return encode_r(0b0000000, rs2, rs1, 0b101, rd, OP_32); }
uint32_t insn_sraw(Register rd, Register rs1, Register rs2) { return encode_r(0b0100000, rs2, rs1, 0b101, rd, OP_32); }

uint32_t insn_addi(Register rd, Register rs1, int32_t imm)  { return encode_i(imm, rs1, 0b000, rd, OP_IMM); }
uint32_t insn_slti(Register rd, Register rs1, int32_t imm)  { return encode_i(imm, rs1, 0b010, rd, OP_IMM); }
uint32_t insn_sltiu(Register rd, Register rs1, int32_t imm) { return encode_i(imm, rs1, 0b011, rd, OP_IMM); }
uint32_t insn_xori(Register rd, Register rs1, int32_t imm)  { return encode_i(imm, rs1, 0b100, rd, OP_IMM); }
uint32_t insn_ori(Register rd, Register rs1, int32_t imm)   { return encode_i(imm, rs1, 0b110, rd, OP_IMM); }
uint32_t insn_andi(Register rd, Register rs1, int32_t imm)  { return encode_i(imm, rs1, 0b111, rd, OP_IMM); }

uint32_t insn_slli(Register rd, Register rs1, uint32_t shamt) {
  return ((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b001 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM;
}
uint32_t insn_srli(Register rd, Register rs1, uint32_t shamt) {
  return ((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM;
}
uint32_t insn_srai(Register rd, Register rs1, uint32_t shamt) {
  return (0b010000 << 26) | ((shamt & 0x3f) << 20) | ((rs1->encoding() & 0x1f) << 15) | (0b101 << 12) | ((rd->encoding() & 0x1f) << 7) | OP_IMM;
}

uint32_t insn_addiw(Register rd, Register rs1, int32_t imm) { return encode_i(imm, rs1, 0b000, rd, OP_IMM_32); }

uint32_t insn_ld(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b011, rd, LOAD); }
uint32_t insn_lw(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b010, rd, LOAD); }
uint32_t insn_lh(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b001, rd, LOAD); }
uint32_t insn_lb(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b000, rd, LOAD); }
uint32_t insn_lwu(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b110, rd, LOAD); }
uint32_t insn_lhu(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b101, rd, LOAD); }
uint32_t insn_lbu(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b100, rd, LOAD); }

uint32_t insn_sd(Register rs2, Register rs1, int32_t offset) { return encode_s(offset, rs2, rs1, 0b011, STORE); }
uint32_t insn_sw(Register rs2, Register rs1, int32_t offset) { return encode_s(offset, rs2, rs1, 0b010, STORE); }
uint32_t insn_sh(Register rs2, Register rs1, int32_t offset) { return encode_s(offset, rs2, rs1, 0b001, STORE); }
uint32_t insn_sb(Register rs2, Register rs1, int32_t offset) { return encode_s(offset, rs2, rs1, 0b000, STORE); }

uint32_t insn_beq(Register rs1, Register rs2, int32_t offset)  { return encode_b(offset, rs2, rs1, 0b000, BRANCH); }
uint32_t insn_bne(Register rs1, Register rs2, int32_t offset)  { return encode_b(offset, rs2, rs1, 0b001, BRANCH); }
uint32_t insn_blt(Register rs1, Register rs2, int32_t offset)  { return encode_b(offset, rs2, rs1, 0b100, BRANCH); }
uint32_t insn_bge(Register rs1, Register rs2, int32_t offset)  { return encode_b(offset, rs2, rs1, 0b101, BRANCH); }
uint32_t insn_bltu(Register rs1, Register rs2, int32_t offset) { return encode_b(offset, rs2, rs1, 0b110, BRANCH); }
uint32_t insn_bgeu(Register rs1, Register rs2, int32_t offset) { return encode_b(offset, rs2, rs1, 0b111, BRANCH); }

uint32_t insn_lui(Register rd, int32_t imm20)   { return encode_u(imm20, rd, LUI); }
uint32_t insn_auipc(Register rd, int32_t imm20) { return encode_u(imm20, rd, AUIPC); }
uint32_t insn_jal(Register rd, int32_t offset)  { return encode_j(offset, rd, JAL); }
uint32_t insn_jalr(Register rd, Register rs1, int32_t offset) { return encode_i(offset, rs1, 0b000, rd, JALR); }

uint32_t insn_mul(Register rd, Register rs1, Register rs2)    { return encode_r(0b0000001, rs2, rs1, 0b000, rd, OP); }
uint32_t insn_div(Register rd, Register rs1, Register rs2)    { return encode_r(0b0000001, rs2, rs1, 0b100, rd, OP); }
uint32_t insn_rem(Register rd, Register rs1, Register rs2)    { return encode_r(0b0000001, rs2, rs1, 0b110, rd, OP); }

uint32_t insn_lr_w(Register rd, Register rs1, bool aq, bool rl) {
  int funct7 = (0b00010 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
  return encode_r(funct7, zr, rs1, 0b010, rd, AMO);
}
uint32_t insn_sc_w(Register rd, Register rs2, Register rs1, bool aq, bool rl) {
  int funct7 = (0b00011 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
  return encode_r(funct7, rs2, rs1, 0b010, rd, AMO);
}
uint32_t insn_amoadd_w(Register rd, Register rs2, Register rs1, bool aq, bool rl) {
  int funct7 = (0b00000 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
  return encode_r(funct7, rs2, rs1, 0b010, rd, AMO);
}
uint32_t insn_amoswap_d(Register rd, Register rs2, Register rs1, bool aq, bool rl) {
  int funct7 = (0b00001 << 2) | ((aq ? 1 : 0) << 1) | (rl ? 1 : 0);
  return encode_r(funct7, rs2, rs1, 0b011, rd, AMO);
}

uint32_t insn_fence(uint32_t pred, uint32_t succ) {
  return ((pred & 0xf) << 24) | ((succ & 0xf) << 20) | (0b000 << 12) | MISC_MEM;
}
uint32_t insn_fence_i() { return (0b001 << 12) | MISC_MEM; }
uint32_t insn_ecall()   { return (0b000 << 12) | SYSTEM; }
uint32_t insn_ebreak()  { return (1 << 20) | (0b000 << 12) | SYSTEM; }

uint32_t insn_fadd_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) {
  return encode_r_fp(0b0000001, rs2, rs1, 0b111, rd, OP_FP);
}
uint32_t insn_fsub_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2) {
  return encode_r_fp(0b0000101, rs2, rs1, 0b111, rd, OP_FP);
}
uint32_t insn_flw(FloatRegister rd, Register rs1, int32_t offset) {
  return encode_i_fp(offset, rs1, 0b010, rd, LOAD_FP);
}
uint32_t insn_fsw(FloatRegister rs2, Register rs1, int32_t offset) {
  return encode_s_fp(offset, rs2, rs1, 0b010, STORE_FP);
}
uint32_t insn_fld(FloatRegister rd, Register rs1, int32_t offset) {
  return encode_i_fp(offset, rs1, 0b011, rd, LOAD_FP);
}
uint32_t insn_fsd(FloatRegister rs2, Register rs1, int32_t offset) {
  return encode_s_fp(offset, rs2, rs1, 0b011, STORE_FP);
}

static uint32_t patch_b_insn(uint32_t insn, int32_t offset) {
  uint32_t imm12 = (offset >> 12) & 0x1;
  uint32_t imm11 = (offset >> 11) & 0x1;
  uint32_t imm10_5 = (offset >> 5) & 0x3f;
  uint32_t imm4_1 = (offset >> 1) & 0xf;
  insn &= ~((1U << 31) | (0x3f << 25) | (0xf << 8) | (1 << 7));
  insn |= (imm12 << 31) | (imm10_5 << 25) | (imm4_1 << 8) | (imm11 << 7);
  return insn;
}

static uint32_t patch_j_insn(uint32_t insn, int32_t offset) {
  uint32_t imm20 = (offset >> 20) & 0x1;
  uint32_t imm19_12 = (offset >> 12) & 0xff;
  uint32_t imm11 = (offset >> 11) & 0x1;
  uint32_t imm10_1 = (offset >> 1) & 0x3ff;
  insn &= 0xfff;
  insn |= (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | (imm19_12 << 12);
  return insn;
}

int main() {
  printf("========================================================\n");
  printf("RISC-V Assembler Differential TDD Verification (GNU as)\n");
  printf("========================================================\n");

  int pass_count = 0;
  int fail_count = 0;

#define RUN_CHECK(ACTUAL_EXPR, GNU_ASM_TEXT) do { \
    uint32_t actual = (ACTUAL_EXPR); \
    char cmd[512]; \
    snprintf(cmd, sizeof(cmd), "echo \"%s\" | riscv64-linux-gnu-as -march=rv64g -o /tmp/tdd.o && riscv64-linux-gnu-objcopy -O binary --only-section=.text /tmp/tdd.o /tmp/tdd.bin", GNU_ASM_TEXT); \
    int ret = system(cmd); \
    if (ret != 0) { \
      fprintf(stderr, "[FAIL] GNU as command failed: %s\n", GNU_ASM_TEXT); \
      fail_count++; \
      break; \
    } \
    FILE* f = fopen("/tmp/tdd.bin", "rb"); \
    if (!f) { \
      fprintf(stderr, "[FAIL] Cannot open /tmp/tdd.bin for: %s\n", GNU_ASM_TEXT); \
      fail_count++; \
      break; \
    } \
    uint32_t gnu_insn = 0; \
    size_t n = fread(&gnu_insn, 1, 4, f); \
    fclose(f); \
    if (n != 4) { \
      fprintf(stderr, "[FAIL] GNU as did not output 4 bytes for: %s (got %zu)\n", GNU_ASM_TEXT, n); \
      fail_count++; \
      break; \
    } \
    if (actual == gnu_insn) { \
      printf("  [PASS] %-30s => 0x%08x\n", GNU_ASM_TEXT, actual); \
      pass_count++; \
    } else { \
      printf("  [FAIL] %-30s => HotSpot: 0x%08x vs GNU: 0x%08x (Diff: 0x%08x)\n", \
             GNU_ASM_TEXT, actual, gnu_insn, actual ^ gnu_insn); \
      fail_count++; \
    } \
  } while(0)

  printf("\n[Group 1: RV64I Integer Arithmetic & Logic (R-Type)]\n");
  RUN_CHECK(insn_add(a0, a1, a2), "add a0, a1, a2");
  RUN_CHECK(insn_sub(a0, a1, a2), "sub a0, a1, a2");
  RUN_CHECK(insn_sll(a0, a1, a2), "sll a0, a1, a2");
  RUN_CHECK(insn_slt(a0, a1, a2), "slt a0, a1, a2");
  RUN_CHECK(insn_sltu(a0, a1, a2), "sltu a0, a1, a2");
  RUN_CHECK(insn_xor(a0, a1, a2), "xor a0, a1, a2");
  RUN_CHECK(insn_srl(a0, a1, a2), "srl a0, a1, a2");
  RUN_CHECK(insn_sra(a0, a1, a2), "sra a0, a1, a2");
  RUN_CHECK(insn_or(a0, a1, a2), "or a0, a1, a2");
  RUN_CHECK(insn_and(a0, a1, a2), "and a0, a1, a2");

  printf("\n[Group 2: RV64I 32-bit Word Variants (R-Type)]\n");
  RUN_CHECK(insn_addw(a0, a1, a2), "addw a0, a1, a2");
  RUN_CHECK(insn_subw(a0, a1, a2), "subw a0, a1, a2");
  RUN_CHECK(insn_sllw(a0, a1, a2), "sllw a0, a1, a2");
  RUN_CHECK(insn_srlw(a0, a1, a2), "srlw a0, a1, a2");
  RUN_CHECK(insn_sraw(a0, a1, a2), "sraw a0, a1, a2");

  printf("\n[Group 3: RV64I Immediates (I-Type)]\n");
  RUN_CHECK(insn_addi(a0, a1, 42), "addi a0, a1, 42");
  RUN_CHECK(insn_addi(a0, a1, -2048), "addi a0, a1, -2048");
  RUN_CHECK(insn_slti(a0, a1, 100), "slti a0, a1, 100");
  RUN_CHECK(insn_sltiu(a0, a1, 100), "sltiu a0, a1, 100");
  RUN_CHECK(insn_xori(a0, a1, 85), "xori a0, a1, 85");
  RUN_CHECK(insn_ori(a0, a1, 170), "ori a0, a1, 170");
  RUN_CHECK(insn_andi(a0, a1, 127), "andi a0, a1, 127");
  RUN_CHECK(insn_slli(a0, a1, 4), "slli a0, a1, 4");
  RUN_CHECK(insn_srli(a0, a1, 5), "srli a0, a1, 5");
  RUN_CHECK(insn_srai(a0, a1, 6), "srai a0, a1, 6");
  RUN_CHECK(insn_addiw(a0, a1, -1), "addiw a0, a1, -1");

  printf("\n[Group 4: RV64I Loads & Stores (I-Type & S-Type)]\n");
  RUN_CHECK(insn_ld(a0, a1, 8), "ld a0, 8(a1)");
  RUN_CHECK(insn_lw(a0, a1, 4), "lw a0, 4(a1)");
  RUN_CHECK(insn_lh(a0, a1, 2), "lh a0, 2(a1)");
  RUN_CHECK(insn_lb(a0, a1, 1), "lb a0, 1(a1)");
  RUN_CHECK(insn_lwu(a0, a1, 4), "lwu a0, 4(a1)");
  RUN_CHECK(insn_lhu(a0, a1, 2), "lhu a0, 2(a1)");
  RUN_CHECK(insn_lbu(a0, a1, 1), "lbu a0, 1(a1)");

  RUN_CHECK(insn_sd(a2, a1, 16), "sd a2, 16(a1)");
  RUN_CHECK(insn_sw(a2, a1, 8), "sw a2, 8(a1)");
  RUN_CHECK(insn_sh(a2, a1, 4), "sh a2, 4(a1)");
  RUN_CHECK(insn_sb(a2, a1, 2), "sb a2, 2(a1)");

  printf("\n[Group 5: RV64I Branches & Control Transfer]\n");
  RUN_CHECK(insn_beq(a0, a1, 32), "beq a0, a1, .+32");
  RUN_CHECK(insn_bne(a0, a1, 16), "bne a0, a1, .+16");
  RUN_CHECK(insn_blt(a0, a1, 8), "blt a0, a1, .+8");
  RUN_CHECK(insn_bge(a0, a1, 4), "bge a0, a1, .+4");
  RUN_CHECK(insn_bltu(a0, a1, 8), "bltu a0, a1, .+8");
  RUN_CHECK(insn_bgeu(a0, a1, 4), "bgeu a0, a1, .+4");
  RUN_CHECK(insn_lui(a0, 74565 << 12), "lui a0, 74565");
  RUN_CHECK(insn_auipc(a0, 1 << 12), "auipc a0, 1");
  RUN_CHECK(insn_jal(ra, 64), "jal ra, .+64");
  RUN_CHECK(insn_jalr(ra, a0, 8), "jalr ra, 8(a0)");

  printf("\n[Group 6: RV64M Multiplication and Division]\n");
  RUN_CHECK(insn_mul(a0, a1, a2), "mul a0, a1, a2");
  RUN_CHECK(insn_div(a0, a1, a2), "div a0, a1, a2");
  RUN_CHECK(insn_rem(a0, a1, a2), "rem a0, a1, a2");

  printf("\n[Group 7: RV64A Atomic Operations]\n");
  RUN_CHECK(insn_lr_w(a0, a1, false, false), "lr.w a0, (a1)");
  RUN_CHECK(insn_lr_w(a0, a1, true, true), "lr.w.aqrl a0, (a1)");
  RUN_CHECK(insn_sc_w(a0, a2, a1, false, false), "sc.w a0, a2, (a1)");
  RUN_CHECK(insn_amoadd_w(a0, a2, a1, false, false), "amoadd.w a0, a2, (a1)");
  RUN_CHECK(insn_amoadd_w(a0, a2, a1, true, true), "amoadd.w.aqrl a0, a2, (a1)");
  RUN_CHECK(insn_amoswap_d(a0, a2, a1, true, true), "amoswap.d.aqrl a0, a2, (a1)");

  printf("\n[Group 8: Barriers & System Instructions]\n");
  RUN_CHECK(insn_fence(0b1111, 0b1111), "fence");
  RUN_CHECK(insn_fence_i(), "fence.i");
  RUN_CHECK(insn_ecall(), "ecall");
  RUN_CHECK(insn_ebreak(), "ebreak");

  printf("\n[Group 9: RV64F/D Floating Point]\n");
  RUN_CHECK(insn_flw(fa0, a1, 4), "flw fa0, 4(a1)");
  RUN_CHECK(insn_fld(fa0, a1, 8), "fld fa0, 8(a1)");
  RUN_CHECK(insn_fsw(fa0, a1, 4), "fsw fa0, 4(a1)");
  RUN_CHECK(insn_fsd(fa0, a1, 8), "fsd fa0, 8(a1)");
  printf("\n[Group 10: MacroAssembler Branch & Jump Label Patching (pd_patch_instruction)]\n");
  RUN_CHECK(patch_b_insn(insn_beq(a0, a1, 0), 32), "beq a0, a1, .+32");
  RUN_CHECK(patch_b_insn(insn_bne(a0, a1, 0), -16), "bne a0, a1, .-16");
  RUN_CHECK(patch_b_insn(insn_blt(a0, a1, 0), 128), "blt a0, a1, .+128");
  RUN_CHECK(patch_b_insn(insn_bge(a0, a1, 0), -64), "bge a0, a1, .-64");
  RUN_CHECK(patch_j_insn(insn_jal(ra, 0), 64), "jal ra, .+64");
  RUN_CHECK(patch_j_insn(insn_jal(zr, 0), -32), "jal zero, .-32");

  printf("\n========================================================\n");
  printf("TDD Test Summary: Total %d | Passed: %d | Failed: %d\n", pass_count + fail_count, pass_count, fail_count);
  printf("========================================================\n");

  return fail_count == 0 ? 0 : 1;
}
