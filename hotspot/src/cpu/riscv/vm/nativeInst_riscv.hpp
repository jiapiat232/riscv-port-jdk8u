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

#ifndef CPU_RISCV_VM_NATIVEINST_RISCV_HPP
#define CPU_RISCV_VM_NATIVEINST_RISCV_HPP

#include "asm/assembler.hpp"
#include "memory/allocation.hpp"
#include "runtime/icache.hpp"
#include "runtime/os.hpp"
#include "utilities/top.hpp"

// We have interfaces for the following instructions:
// - NativeInstruction
// - - NativeCall
// - - NativeMovConstReg
// - - NativeMovRegMem
// - - NativeJump
// - - - NativeGeneralJump
// - - NativeIllegalInstruction
// - - NativeCallTrampolineStub
// - - NativeMembar
// - - NativeFenceI

// The base class for different kinds of native instruction abstractions.
// Provides the primitive operations to manipulate code relative to this.

class NativeInstruction {
  friend class Relocation;
  friend bool is_NativeCallTrampolineStub_at(address);
 public:
  enum {
    instruction_size = 4
  };

  juint encoding() const {
    return uint_at(0);
  }

  bool is_jal()                             const { return is_jal_at(addr_at(0));         }
  bool is_jalr()                            const { return is_jalr_at(addr_at(0));        }
  bool is_movptr()                          const { return is_movptr_at(addr_at(0));      }
  bool is_call()                            const { return is_call_at(addr_at(0));        }
  bool is_jump()                            const { return is_jump_at(addr_at(0));        }

  static bool is_jal_at(address addr)             { return extract_opcode(addr) == 0b1101111; }
  static bool is_jalr_at(address addr)            { return extract_opcode(addr) == 0b1100111 && extract_funct3(addr) == 0b000; }
  static bool is_branch_at(address addr)          { return extract_opcode(addr) == 0b1100011; }
  static bool is_ld_at(address addr)              { return extract_opcode(addr) == 0b0000011 && extract_funct3(addr) == 0b011; }
  static bool is_auipc_at(address addr)           { return extract_opcode(addr) == 0b0010111; }
  static bool is_jump_at(address addr)            { return is_jal_at(addr) || is_jalr_at(addr); }
  static bool is_call_at(address addr)            { return is_jal_at(addr) && extract_rd(addr) == x1; } // jal ra, offset
  static bool is_lwu_to_zr(address addr);
  static bool is_pc_relative_at(address branch);
  static bool is_movptr_at(address branch);
  static bool is_li32_at(address branch);
  static bool is_load_pc_relative_at(address branch);
  static bool maybe_cpool_ref(address instr) {
    return is_load_pc_relative_at(instr);
  }

  inline bool is_nop();
  inline bool is_illegal();
  inline bool is_return();
  inline bool is_cond_jump();
  bool is_safepoint_poll();
  bool is_sigill_zombie_not_entrant();
  bool is_stop();

 protected:
  address addr_at(int offset) const    { return address(this) + offset; }

  s_char sbyte_at(int offset) const    { return *(s_char*) addr_at(offset); }
  u_char ubyte_at(int offset) const    { return *(u_char*) addr_at(offset); }
  jint int_at(int offset) const        { return *(jint*) addr_at(offset); }
  juint uint_at(int offset) const      { return *(juint*) addr_at(offset); }
  address ptr_at(int offset) const     { return *(address*) addr_at(offset); }
  oop  oop_at (int offset) const       { return *(oop*) addr_at(offset); }

  void set_char_at(int offset, char c)        { *addr_at(offset) = (u_char)c; }
  void set_int_at(int offset, jint i)         { *(jint*)addr_at(offset) = i; }
  void set_uint_at(int offset, jint i)        { *(juint*)addr_at(offset) = i; }
  void set_ptr_at (int offset, address ptr)   { *(address*) addr_at(offset) = ptr; }
  void set_oop_at (int offset, oop o)         { *(oop*) addr_at(offset) = o; }

  int32_t insn_word() const { return int_at(0); }

  static Register extract_rs1(address addr);
  static Register extract_rs2(address addr);
  static Register extract_rd(address addr);
  static uint32_t extract_opcode(address addr);
  static uint32_t extract_funct3(address addr);

 public:
  void print();
  friend inline NativeInstruction* nativeInstruction_at(address address);
};

inline NativeInstruction* nativeInstruction_at(address address) {
  return (NativeInstruction*)address;
}

// The NativeCall is an abstraction for accessing/manipulating native
// call jal instructions (used to manipulate inline caches, primitive &
// DSO calls, etc.).
class NativeCall: public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    instruction_size            = 4,
    instruction_offset          = 0,
    displacement_offset         = 0,
    return_address_offset       = 4
  };

  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  { return addr_at(return_address_offset); }
  address return_address() const            { return addr_at(return_address_offset); }
  address destination() const;

  void set_destination(address dest) {
    assert(is_jal(), "Should be jal instruction!");
    intptr_t offset = (intptr_t)(dest - instruction_address());
    assert((offset & 0x1) == 0, "bad alignment");
    assert(Assembler::is_simm21(offset), "encoding constraint");
    unsigned int insn = 0b1101111; // jal
    address pInsn = (address)(&insn);
    Assembler::patch(pInsn, 31, 31, (offset >> 20) & 0x1);
    Assembler::patch(pInsn, 30, 21, (offset >> 1) & 0x3ff);
    Assembler::patch(pInsn, 20, 20, (offset >> 11) & 0x1);
    Assembler::patch(pInsn, 19, 12, (offset >> 12) & 0xff);
    Assembler::patch_reg(pInsn, 11, 7, ra); // Rd must be x1, need ra
    set_int_at(displacement_offset, insn);
  }

  void verify_alignment()                       {} // do nothing on riscv
  void verify();
  void print();

  // Creation
  friend inline NativeCall* nativeCall_at(address address);
  friend inline NativeCall* nativeCall_before(address return_address);

  static bool is_call_before(address return_address) {
    return is_call_at(return_address - NativeCall::return_address_offset);
  }

  // MT-safe patching of a call instruction.
  static void insert(address code_pos, address entry);

  static void replace_mt_safe(address instr_addr, address code_buffer);

  // Similar to replace_mt_safe, but just changes the destination.  The
  // important thing is that free-running threads are able to execute
  // this call instruction at all times.  If the call is diagnostic
  // Simply rely on monotonic behavior of word writes.
  //
  // Used in the runtime linkage of calls; see class CompiledIC.
  // (Cf. 4506997 and 4479829, where threads witnessed garbage displacements.)

  // The parameter assert_lock disables the assertion during code generation.
  void set_destination_mt_safe(address dest, bool assert_lock = true);

  address get_trampoline();
  address trampoline_jump(CodeBuffer &cbuf, address dest);
};

inline NativeCall* nativeCall_at(address address) {
  NativeCall* call = (NativeCall*)(address - NativeCall::instruction_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

inline NativeCall* nativeCall_before(address return_address) {
  NativeCall* call = (NativeCall*)(return_address - NativeCall::return_address_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

// An interface for accessing/manipulating native mov reg, imm instructions.
// (used to manipulate inlined 64-bit data calls, etc.)
class NativeMovConstReg: public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    movptr_instruction_size             =    6 * NativeInstruction::instruction_size, // lui, addi, slli, addi, slli, addi.
    load_pc_relative_instruction_size   =    2 * NativeInstruction::instruction_size, // auipc, ld
    li32_instruction_size               =    2 * NativeInstruction::instruction_size, // lui, addiw
    instruction_offset                  =    0,
  };

  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  {
    if (NativeInstruction::is_movptr_at(instruction_address())) {
      return addr_at(movptr_instruction_size);
    } else if (NativeInstruction::is_load_pc_relative_at(instruction_address())) {
      return addr_at(load_pc_relative_instruction_size);
    } else if (NativeInstruction::is_li32_at(instruction_address())) {
      return addr_at(li32_instruction_size);
    } else {
      guarantee(false, "Unknown instruction in NativeMovConstReg");
      return NULL;
    }
  }

  intptr_t data() const;
  void  set_data(intptr_t x);

  void flush() {
    if (!maybe_cpool_ref(instruction_address())) {
      ICache::invalidate_range(instruction_address(), movptr_instruction_size);
    }
  }

  void  verify();
  void  print();

  // Creation
  friend inline NativeMovConstReg* nativeMovConstReg_at(address address);
  friend inline NativeMovConstReg* nativeMovConstReg_before(address address);
};

inline NativeMovConstReg* nativeMovConstReg_at(address address) {
  NativeMovConstReg* test = (NativeMovConstReg*)(address - NativeMovConstReg::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

inline NativeMovConstReg* nativeMovConstReg_before(address address) {
  NativeMovConstReg* test = (NativeMovConstReg*)(address - NativeMovConstReg::instruction_size - NativeMovConstReg::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

class NativeMovRegMem: public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    instruction_size            = NativeMovConstReg::movptr_instruction_size,
    instruction_offset          = 0,
    data_offset                 = 0,
    next_instruction_offset     = NativeMovConstReg::movptr_instruction_size
  };

  int instruction_start() const { return instruction_offset; }

  address instruction_address() const { return addr_at(instruction_offset); }

  int num_bytes_to_end_of_patch() const { return instruction_offset + instruction_size; }

  int offset() const;

  void set_offset(int x);

  void add_offset_in_bytes(int add_offset) {
    set_offset(offset() + add_offset);
  }

  void verify();
  void print();

 private:
  friend inline NativeMovRegMem* nativeMovRegMem_at(address address);
};

inline NativeMovRegMem* nativeMovRegMem_at(address address) {
  NativeMovRegMem* test = (NativeMovRegMem*)(address - NativeMovRegMem::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

// An interface for accessing/manipulating native jumps
class NativeJump: public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    instruction_size            = 4,
    instruction_offset          = 0,
    data_offset                 = 0,
    next_instruction_offset     = 4
  };

  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  { return addr_at(instruction_size); }
  address jump_destination() const;
  void set_jump_destination(address dest);

  // Creation
  friend inline NativeJump* nativeJump_at(address address);

  void verify();

  // Unit testing stuff
  static void test() {}

  // Insertion of native jump instruction
  static void insert(address code_pos, address entry);
  // MT-safe insertion of native jump at verified method entry
  static void check_verified_entry_alignment(address entry, address verified_entry);
  static void patch_verified_entry(address entry, address verified_entry, address dest);
};

inline NativeJump* nativeJump_at(address address) {
  NativeJump* jump = (NativeJump*)(address - NativeJump::instruction_offset);
#ifdef ASSERT
  jump->verify();
#endif
  return jump;
}

class NativeGeneralJump: public NativeJump {
 public:
  enum RISCV_specific_constants {
    instruction_size            = 6 * NativeInstruction::instruction_size, // lui, addi, slli, addi, slli, jalr
    instruction_offset          = 0,
    data_offset                 = 0,
    next_instruction_offset     = 6 * NativeInstruction::instruction_size  // lui, addi, slli, addi, slli, jalr
  };

  address jump_destination() const;
  static void insert_unconditional(address code_pos, address entry);
  static void replace_mt_safe(address instr_addr, address code_buffer);
  void verify();
};

inline NativeGeneralJump* nativeGeneralJump_at(address address) {
  NativeGeneralJump* jump = (NativeGeneralJump*)(address);
  debug_only(jump->verify();)
  return jump;
}

class NativeIllegalInstruction: public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    instruction_code            = 0xffffffff, // 4-byte illegal instruction triggers SIGILL
    instruction_size            = 4,
    instruction_offset          = 0,
    next_instruction_offset     = 4
  };

  // Insert illegal opcode as specific address
  static void insert(address code_pos);
};

inline bool NativeInstruction::is_nop()         { return (uint_at(0) & 0xffffffff) == 0x00000013; }
inline bool NativeInstruction::is_illegal()     { return (uint_at(0) & 0xffffffff) == NativeIllegalInstruction::instruction_code; }
inline bool NativeInstruction::is_return()      { return (uint_at(0) & 0xffffffff) == 0x00008067; } // jalr zero, 0(ra)
inline bool NativeInstruction::is_cond_jump()   { return is_branch_at(addr_at(0)); }

// Call trampoline stubs.
class NativeCallTrampolineStub : public NativeInstruction {
 public:

  enum RISCV_specific_constants {
    // 3 * 4B instructions (auipc + ld + jr) + 8B target address = 20 bytes
    instruction_size            = 3 * NativeInstruction::instruction_size + wordSize,
    instruction_offset          = 0,
    data_offset                 = 3 * NativeInstruction::instruction_size,
    next_instruction_offset     = 3 * NativeInstruction::instruction_size + wordSize
  };

  address destination(nmethod *nm = NULL) const;
  void set_destination(address new_destination);
  ptrdiff_t destination_offset() const;
};

inline bool is_NativeCallTrampolineStub_at(address addr) {
  // Ensure that the stub contains:
  //      ld   t0, L (auipc + ld)
  //      jr   t0
  // L:   .dword dest
  uint32_t* i = (uint32_t*)addr;
  return NativeInstruction::is_load_pc_relative_at((address)i) &&
         NativeInstruction::extract_rd((address)(i + 1)) == x5 &&
         NativeInstruction::is_jalr_at((address)(i + 2))   &&
         NativeInstruction::extract_rs1((address)(i + 2)) == x5 &&
         NativeInstruction::extract_rd((address)(i + 2)) == x0;
}

inline NativeCallTrampolineStub* nativeCallTrampolineStub_at(address addr) {
  assert(is_NativeCallTrampolineStub_at(addr), "no call trampoline found");
  return (NativeCallTrampolineStub*)addr;
}

class NativeMembar : public NativeInstruction {
 public:
  uint32_t get_kind();
  void set_kind(uint32_t order_kind);
};

inline NativeMembar *NativeMembar_at(address addr) {
  assert((*(uint32_t*)addr & 0x7f) == 0b0001111 && (*(uint32_t*)addr & (0b111 << 12)) == 0, "no membar found");
  return (NativeMembar*)addr;
}

class NativeFenceI : public NativeInstruction {
 public:
  enum RISCV_specific_constants {
    instruction_size = 1 * NativeInstruction::instruction_size
  };
};

#endif // CPU_RISCV_VM_NATIVEINST_RISCV_HPP
