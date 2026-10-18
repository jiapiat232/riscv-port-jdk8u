/*
 * Copyright (c) 1997, 2026, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2018, Red Hat Inc. All rights reserved.
 * Copyright (c) 2020, 2023, Huawei Technologies Co., Ltd. All rights reserved.
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

class NativeCall;

class NativeInstruction VALUE_OBJ_CLASS_SPEC {
  friend class Relocation;
 public:
  enum {
    instruction_size = 4,
    compressed_instruction_size = 2
  };

  address addr_at(int offset) const { return (address) this + offset; }

  juint encoding() const {
    return *(juint*) this;
  }

  bool is_nop() const {
    return encoding() == 0x00000013; // addi x0, x0, 0
  }

  bool is_sigill_zombie_not_entrant() const {
    return encoding() == 0x00000000;
  }

  bool is_stop() const {
    // csrw time, x0 (0xc0101073)
    return encoding() == 0xc0101073;
  }

  bool is_jump() const {
    return (encoding() & 0x7f) == 0x6f; // JAL
  }

  bool is_jump_or_nop() const {
    return is_nop() || is_jump();
  }

  bool is_safepoint_poll() const {
    return (encoding() & 0x7fff) == 0x6003;
  }
};

inline NativeInstruction* nativeInstruction_at(address address) {
  return (NativeInstruction*) address;
}

class NativeCall: public NativeInstruction {
 public:
  enum {
    instruction_size = 8 // auipc + jalr
  };

  static bool is_call_at(address addr) {
    if (addr == NULL) return false;
    uint32_t insn = *(uint32_t*)addr;
    return ((insn & 0x7fff) == 0x00e7) || ((insn & 0xfff) == 0x00ef);
  }

  static bool is_call_before(address return_address) {
    if (return_address == NULL) return false;
    return is_call_at(return_address - 4) || is_call_at(return_address - instruction_size);
  }

  address instruction_address() const { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }
  address return_address() const { return next_instruction_address(); }
  address destination() const;
  void set_destination(address new_destination);
  void set_destination_mt_safe(address dest, bool assert_lock = true);
  void verify_alignment() {}
  void verify();
};

inline NativeCall* nativeCall_at(address address) {
  NativeCall* call = (NativeCall*) address;
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

inline NativeCall* nativeCall_before(address return_address) {
  NativeCall* call = (NativeCall*) (return_address - NativeCall::instruction_size);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

class NativeMovConstReg: public NativeInstruction {
 public:
  enum {
    instruction_size = 24
  };

  address instruction_address() const      { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }

  intptr_t data() const;
  void set_data(intptr_t x);

  void verify();
  void print();

  inline friend NativeMovConstReg* nativeMovConstReg_at(address addr);
  inline friend NativeMovConstReg* nativeMovConstReg_before(address addr);
};

inline NativeMovConstReg* nativeMovConstReg_at(address addr) {
  return (NativeMovConstReg*)(addr);
}

inline NativeMovConstReg* nativeMovConstReg_before(address addr) {
  return (NativeMovConstReg*)(addr - NativeMovConstReg::instruction_size);
}

class NativeMovRegMem: public NativeInstruction {
 public:
  int num_bytes_to_end_of_patch() const { Unimplemented(); return 0; }
  int offset() const                    { Unimplemented(); return 0; }
  void set_offset(int x)                { Unimplemented(); }
  void add_offset_in_bytes(int add_off) { Unimplemented(); }
};

inline NativeMovRegMem* nativeMovRegMem_at(address addr) {
  Unimplemented();
  return (NativeMovRegMem*)NULL;
}

class NativeJump: public NativeInstruction {
 public:
  enum {
    instruction_size = 8
  };

  address instruction_address() const      { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }
  address jump_destination() const;
  void set_jump_destination(address dest);
  void verify();
  static void check_verified_entry_alignment(address entry, address verified_entry);
  static void patch_verified_entry(address entry, address verified_entry, address dest);
};

inline NativeJump* nativeJump_at(address addr) {
  return (NativeJump*)addr;
}

class NativeGeneralJump: public NativeJump {
 public:
  enum {
    instruction_size = 8
  };
  static void insert_unconditional(address code_pos, address entry);
  static void replace_mt_safe(address instr_addr, address code_buffer);
  static void verify();
};

inline NativeGeneralJump* nativeGeneralJump_at(address address) {
  return (NativeGeneralJump*)(address);
}

#endif // CPU_RISCV_VM_NATIVEINST_RISCV_HPP
