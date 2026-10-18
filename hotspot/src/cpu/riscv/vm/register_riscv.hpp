/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 2000, 2023, Oracle and/or its affiliates.
 * Copyright (c) 2020, 2023, Huawei Technologies Co., Ltd.
 * All rights reserved.
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

#ifndef CPU_RISCV_VM_REGISTER_RISCV_HPP
#define CPU_RISCV_VM_REGISTER_RISCV_HPP

#include "asm/register.hpp"
#include "vm_version_riscv.hpp"
#include "utilities/top.hpp"

// CSR (Control and Status Register) addresses
#define CSR_FFLAGS   0x001        // Floating-Point Accrued Exceptions
#define CSR_FRM      0x002        // Floating-Point Dynamic Rounding Mode
#define CSR_FCSR     0x003        // Floating-Point Control and Status Register
#define CSR_VSTART   0x008        // Vector start position
#define CSR_VXSAT    0x009        // Fixed-Point Saturate Flag
#define CSR_VXRM     0x00A        // Fixed-Point Rounding Mode
#define CSR_VCSR     0x00F        // Vector control and status register
#define CSR_VL       0xC20        // Vector length
#define CSR_VTYPE    0xC21        // Vector data type register
#define CSR_VLENB    0xC22        // VLEN/8 (vector register length in bytes)
#define CSR_CYCLE    0xC00        // Cycle counter for RDCYCLE
#define CSR_TIME     0xC01        // Timer for RDTIME
#define CSR_INSTRET  0xC02        // Instructions-retired counter for RDINSTRET

class VMRegImpl;
typedef VMRegImpl* VMReg;

class RegisterImpl;
typedef RegisterImpl* Register;

inline Register as_Register(int encoding) {
  return (Register)(intptr_t) encoding;
}

class RegisterImpl: public AbstractRegisterImpl {
 public:
  enum {
    number_of_registers      = 32,
    number_of_byte_registers = 32,
    max_slots_per_register   = 2,

    // RVC compressed registers: x8 ~ x15 (s0/fp, s1, a0 ~ a5)
    compressed_register_base = 8,
    compressed_register_top  = 15
  };

  Register successor() const { return as_Register(encoding() + 1); }

  inline friend Register as_Register(int encoding);

  VMReg as_VMReg();

  // accessors
  int   encoding() const          { assert(is_valid(), "invalid register"); return (intptr_t)this; }
  bool  is_valid() const          { return 0 <= (intptr_t)this && (intptr_t)this < number_of_registers; }
  bool  has_byte_register() const { return 0 <= (intptr_t)this && (intptr_t)this < number_of_byte_registers; }
  const char* name() const;
  int   encoding_nocheck() const  { return (intptr_t)this; }

  // for RVC compressed instructions
  int compressed_raw_encoding() const {
    return encoding_nocheck() - compressed_register_base;
  }

  int compressed_encoding() const {
    assert(is_compressed_valid(), "invalid compressed register");
    return encoding() - compressed_register_base;
  }

  bool is_compressed_valid() const {
    return encoding_nocheck() >= compressed_register_base &&
           encoding_nocheck() <= compressed_register_top;
  }

  // Return the bit representing this register in a bitmask
  unsigned long bit(bool should_set = true) const {
    return should_set ? (1UL << encoding()) : 0;
  }
};

// The integer registers of the RISC-V architecture
CONSTANT_REGISTER_DECLARATION(Register, noreg, (-1));

CONSTANT_REGISTER_DECLARATION(Register, x0,   ( 0));
CONSTANT_REGISTER_DECLARATION(Register, x1,   ( 1));
CONSTANT_REGISTER_DECLARATION(Register, x2,   ( 2));
CONSTANT_REGISTER_DECLARATION(Register, x3,   ( 3));
CONSTANT_REGISTER_DECLARATION(Register, x4,   ( 4));
CONSTANT_REGISTER_DECLARATION(Register, x5,   ( 5));
CONSTANT_REGISTER_DECLARATION(Register, x6,   ( 6));
CONSTANT_REGISTER_DECLARATION(Register, x7,   ( 7));
CONSTANT_REGISTER_DECLARATION(Register, x8,   ( 8));
CONSTANT_REGISTER_DECLARATION(Register, x9,   ( 9));
CONSTANT_REGISTER_DECLARATION(Register, x10,  (10));
CONSTANT_REGISTER_DECLARATION(Register, x11,  (11));
CONSTANT_REGISTER_DECLARATION(Register, x12,  (12));
CONSTANT_REGISTER_DECLARATION(Register, x13,  (13));
CONSTANT_REGISTER_DECLARATION(Register, x14,  (14));
CONSTANT_REGISTER_DECLARATION(Register, x15,  (15));
CONSTANT_REGISTER_DECLARATION(Register, x16,  (16));
CONSTANT_REGISTER_DECLARATION(Register, x17,  (17));
CONSTANT_REGISTER_DECLARATION(Register, x18,  (18));
CONSTANT_REGISTER_DECLARATION(Register, x19,  (19));
CONSTANT_REGISTER_DECLARATION(Register, x20,  (20));
CONSTANT_REGISTER_DECLARATION(Register, x21,  (21));
CONSTANT_REGISTER_DECLARATION(Register, x22,  (22));
CONSTANT_REGISTER_DECLARATION(Register, x23,  (23));
CONSTANT_REGISTER_DECLARATION(Register, x24,  (24));
CONSTANT_REGISTER_DECLARATION(Register, x25,  (25));
CONSTANT_REGISTER_DECLARATION(Register, x26,  (26));
CONSTANT_REGISTER_DECLARATION(Register, x27,  (27));
CONSTANT_REGISTER_DECLARATION(Register, x28,  (28));
CONSTANT_REGISTER_DECLARATION(Register, x29,  (29));
CONSTANT_REGISTER_DECLARATION(Register, x30,  (30));
CONSTANT_REGISTER_DECLARATION(Register, x31,  (31));

// Standard RISC-V ABI aliases
const Register zr       = x0;
const Register ra       = x1;
const Register sp       = x2;
const Register gp       = x3;
const Register tp       = x4;
const Register t0       = x5;
const Register t1       = x6;
const Register t2       = x7;
const Register fp       = x8;
const Register a0       = x10;
const Register a1       = x11;
const Register a2       = x12;
const Register a3       = x13;
const Register a4       = x14;
const Register a5       = x15;
const Register a6       = x16;
const Register a7       = x17;
const Register t3       = x28;
const Register t4       = x29;
const Register t5       = x30;
const Register t6       = x31;

// Callee-saved aliases (prefixed with 'x' to avoid globalDefinitions.hpp s1/s2/s4/s8 conflict)
const Register xs0      = x8;
const Register xs1      = x9;
const Register xs2      = x18;
const Register xs3      = x19;
const Register xs4      = x20;
const Register xs5      = x21;
const Register xs6      = x22;
const Register xs7      = x23;
const Register xs8      = x24;
const Register xs9      = x25;
const Register xs10     = x26;
const Register xs11     = x27;

// C calling convention argument aliases
const Register c_rarg0  = a0;
const Register c_rarg1  = a1;
const Register c_rarg2  = a2;
const Register c_rarg3  = a3;
const Register c_rarg4  = a4;
const Register c_rarg5  = a5;
const Register c_rarg6  = a6;
const Register c_rarg7  = a7;

// Java calling convention argument aliases
const Register j_rarg0  = c_rarg0;
const Register j_rarg1  = c_rarg1;
const Register j_rarg2  = c_rarg2;
const Register j_rarg3  = c_rarg3;
const Register j_rarg4  = c_rarg4;
const Register j_rarg5  = c_rarg5;
const Register j_rarg6  = c_rarg6;
const Register j_rarg7  = c_rarg7;

// HotSpot interpreter dedicated registers
const Register esp       = x20; // x20 (s4): Java expression stack pointer
const Register xdispatch = x21; // x21 (s5): Dispatch table base
const Register xbcp      = x22; // x22 (s6): bytecode pointer
const Register xthread   = x23; // x23 (s7): JavaThread* pointer
const Register rthread   = x23;
const Register xlocals   = x24; // x24 (s8): locals on stack
const Register xmonitors = x25; // x25 (s9): monitors allocated on stack
const Register xcpool    = x26; // x26 (s10): constant pool cache
const Register xheapbase = x27; // x27 (s11): narrow_oop_base for compressed oops
const Register rheapbase = x27;
const Register xmethod   = x31; // x31 (t6): current method

// Floating-point registers
class FloatRegisterImpl;
typedef FloatRegisterImpl* FloatRegister;

inline FloatRegister as_FloatRegister(int encoding) {
  return (FloatRegister)(intptr_t) encoding;
}

class FloatRegisterImpl: public AbstractRegisterImpl {
 public:
  enum {
    number_of_registers    = 32,
    max_slots_per_register = 2,

    // RVC compressed float registers: f8 ~ f15 (fs0, fs1, fa0 ~ fa5)
    compressed_register_base = 8,
    compressed_register_top  = 15
  };

  FloatRegister successor() const { return as_FloatRegister(encoding() + 1); }

  inline friend FloatRegister as_FloatRegister(int encoding);

  VMReg as_VMReg();

  int   encoding() const         { assert(is_valid(), "invalid register"); return (intptr_t)this; }
  int   encoding_nocheck() const { return (intptr_t)this; }
  bool  is_valid() const         { return 0 <= (intptr_t)this && (intptr_t)this < number_of_registers; }
  const char* name() const;

  int compressed_raw_encoding() const {
    return encoding_nocheck() - compressed_register_base;
  }

  int compressed_encoding() const {
    assert(is_compressed_valid(), "invalid compressed register");
    return encoding() - compressed_register_base;
  }

  bool is_compressed_valid() const {
    return encoding_nocheck() >= compressed_register_base &&
           encoding_nocheck() <= compressed_register_top;
  }

  unsigned long bit(bool should_set = true) const {
    return should_set ? (1UL << encoding()) : 0;
  }
};

CONSTANT_REGISTER_DECLARATION(FloatRegister, fnoreg, (-1));

CONSTANT_REGISTER_DECLARATION(FloatRegister, f0,  ( 0));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f1,  ( 1));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f2,  ( 2));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f3,  ( 3));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f4,  ( 4));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f5,  ( 5));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f6,  ( 6));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f7,  ( 7));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f8,  ( 8));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f9,  ( 9));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f10, (10));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f11, (11));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f12, (12));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f13, (13));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f14, (14));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f15, (15));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f16, (16));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f17, (17));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f18, (18));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f19, (19));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f20, (20));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f21, (21));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f22, (22));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f23, (23));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f24, (24));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f25, (25));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f26, (26));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f27, (27));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f28, (28));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f29, (29));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f30, (30));
CONSTANT_REGISTER_DECLARATION(FloatRegister, f31, (31));

// Standard RISC-V Float ABI aliases
const FloatRegister ft0  = f0;
const FloatRegister ft1  = f1;
const FloatRegister ft2  = f2;
const FloatRegister ft3  = f3;
const FloatRegister ft4  = f4;
const FloatRegister ft5  = f5;
const FloatRegister ft6  = f6;
const FloatRegister ft7  = f7;
const FloatRegister fs0  = f8;
const FloatRegister fs1  = f9;
const FloatRegister fa0  = f10;
const FloatRegister fa1  = f11;
const FloatRegister fa2  = f12;
const FloatRegister fa3  = f13;
const FloatRegister fa4  = f14;
const FloatRegister fa5  = f15;
const FloatRegister fa6  = f16;
const FloatRegister fa7  = f17;
const FloatRegister fs2  = f18;
const FloatRegister fs3  = f19;
const FloatRegister fs4  = f20;
const FloatRegister fs5  = f21;
const FloatRegister fs6  = f22;
const FloatRegister fs7  = f23;
const FloatRegister fs8  = f24;
const FloatRegister fs9  = f25;
const FloatRegister fs10 = f26;
const FloatRegister fs11 = f27;
const FloatRegister ft8  = f28;
const FloatRegister ft9  = f29;
const FloatRegister ft10 = f30;
const FloatRegister ft11 = f31;

// C calling convention float argument aliases
const FloatRegister c_farg0 = fa0;
const FloatRegister c_farg1 = fa1;
const FloatRegister c_farg2 = fa2;
const FloatRegister c_farg3 = fa3;
const FloatRegister c_farg4 = fa4;
const FloatRegister c_farg5 = fa5;
const FloatRegister c_farg6 = fa6;
const FloatRegister c_farg7 = fa7;

// Java calling convention float argument aliases
const FloatRegister j_farg0 = c_farg0;
const FloatRegister j_farg1 = c_farg1;
const FloatRegister j_farg2 = c_farg2;
const FloatRegister j_farg3 = c_farg3;
const FloatRegister j_farg4 = c_farg4;
const FloatRegister j_farg5 = c_farg5;
const FloatRegister j_farg6 = c_farg6;
const FloatRegister j_farg7 = c_farg7;

// ConcreteRegisterImpl: exports total number of concrete registers for SharedInfo
class ConcreteRegisterImpl : public AbstractRegisterImpl {
 public:
  enum {
    number_of_registers = (2 * RegisterImpl::number_of_registers +
                           2 * FloatRegisterImpl::number_of_registers +
                           1) // flags
  };

  static const int max_gpr;
  static const int max_fpr;
};

// Bitset for managing groups of registers
class RegSet {
 private:
  uint32_t _bitset;

  RegSet(uint32_t bitset) : _bitset(bitset) { }

 public:
  RegSet() : _bitset(0) { }
  RegSet(Register r1) : _bitset(r1->bit()) { }

  RegSet operator+(const RegSet aSet) const {
    return RegSet(_bitset | aSet._bitset);
  }

  RegSet operator-(const RegSet aSet) const {
    return RegSet(_bitset & ~aSet._bitset);
  }

  RegSet &operator+=(const RegSet aSet) {
    *this = *this + aSet;
    return *this;
  }

  static RegSet of(Register r1) {
    return RegSet(r1);
  }

  static RegSet of(Register r1, Register r2) {
    return of(r1) + r2;
  }

  static RegSet of(Register r1, Register r2, Register r3) {
    return of(r1, r2) + r3;
  }

  static RegSet of(Register r1, Register r2, Register r3, Register r4) {
    return of(r1, r2, r3) + r4;
  }

  static RegSet range(Register start, Register end) {
    uint32_t bits = ~0;
    bits <<= start->encoding();
    bits <<= 31 - end->encoding();
    bits >>= 31 - end->encoding();
    return RegSet(bits);
  }

  uint32_t bits() const { return _bitset; }
};

#endif // CPU_RISCV_VM_REGISTER_RISCV_HPP
