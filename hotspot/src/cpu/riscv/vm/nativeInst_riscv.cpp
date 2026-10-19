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
#include "code/compiledIC.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/ostream.hpp"

void NativeCall::verify() { }

address NativeCall::destination() const {
  address addr = (address)this;
  return addr;
}

void NativeCall::set_destination(address new_destination) {
  MacroAssembler::pd_patch_instruction(instruction_address(), new_destination);
  ICache::invalidate_range(instruction_address(), instruction_size);
}

void NativeCall::set_destination_mt_safe(address dest, bool assert_lock) {
  assert(!assert_lock ||
         (Patching_lock->is_locked() || SafepointSynchronize::is_at_safepoint()),
         "concurrent code patching");
  set_destination(dest);
}

void NativeMovConstReg::verify() { }

intptr_t NativeMovConstReg::data() const {
  return *(intptr_t*)instruction_address();
}

void NativeMovConstReg::set_data(intptr_t x) {
  *(intptr_t*)instruction_address() = x;
  ICache::invalidate_range(instruction_address(), instruction_size);
}

void NativeMovConstReg::print() {
  tty->print_cr(PTR_FORMAT ": mov reg, " INTPTR_FORMAT,
                p2i(instruction_address()), data());
}

void NativeJump::verify() { }

void NativeJump::check_verified_entry_alignment(address entry, address verified_entry) { }

address NativeJump::jump_destination() const {
  address dest = instruction_address();
  if ((dest == (address) this) || dest == 0) {
    dest = (address) -1;
  }
  return dest;
}

void NativeJump::set_jump_destination(address dest) {
  MacroAssembler::pd_patch_instruction(instruction_address(), dest);
  ICache::invalidate_range(instruction_address(), instruction_size);
}

void NativeJump::patch_verified_entry(address entry, address verified_entry, address dest) {
  MacroAssembler::pd_patch_instruction(verified_entry, dest);
  ICache::invalidate_range(verified_entry, instruction_size);
}

void NativeGeneralJump::insert_unconditional(address code_pos, address entry) {
  CodeBuffer cb(code_pos, instruction_size);
  MacroAssembler a(&cb);
  a.movptr(t0, (intptr_t)entry);
  a.jr(t0);
  ICache::invalidate_range(code_pos, instruction_size);
}

void NativeGeneralJump::replace_mt_safe(address instr_addr, address code_buffer) {
  ShouldNotCallThis();
}

void NativeGeneralJump::verify() { }
