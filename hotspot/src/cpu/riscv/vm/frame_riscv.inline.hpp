/*
 * Copyright (c) 1997, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_RISCV_VM_FRAME_RISCV_INLINE_HPP
#define CPU_RISCV_VM_FRAME_RISCV_INLINE_HPP

#include "code/codeCache.hpp"

// Inline functions for RISCV frames:

// Constructors:

inline frame::frame() {
  _pc = NULL;
  _sp = NULL;
  _unextended_sp = NULL;
  _fp = NULL;
  _cb = NULL;
  _deopt_state = unknown;
}

inline void frame::init(intptr_t* sp, intptr_t* fp, address pc) {
  _sp = sp;
  _unextended_sp = sp;
  _fp = fp;
  _pc = pc;
  _cb = CodeCache::find_blob(pc);
  adjust_unextended_sp();

  address original_pc = nmethod::get_deopt_original_pc(this);
  if (original_pc != NULL) {
    _pc = original_pc;
    _deopt_state = is_deoptimized;
  } else {
    _deopt_state = not_deoptimized;
  }
}

inline frame::frame(intptr_t* sp, intptr_t* fp, address pc) {
  init(sp, fp, pc);
}

inline frame::frame(intptr_t* sp, intptr_t* unextended_sp, intptr_t* fp, address pc) {
  _sp = sp;
  _unextended_sp = unextended_sp;
  _fp = fp;
  _pc = pc;
  _cb = CodeCache::find_blob(pc);
  adjust_unextended_sp();

  address original_pc = nmethod::get_deopt_original_pc(this);
  if (original_pc != NULL) {
    _pc = original_pc;
    _deopt_state = is_deoptimized;
  } else {
    _deopt_state = not_deoptimized;
  }
}

inline frame::frame(intptr_t* sp, intptr_t* fp) {
  _sp = sp;
  _unextended_sp = sp;
  _fp = fp;
  _pc = (address)(sp[-1]);
  _cb = CodeCache::find_blob(_pc);
  adjust_unextended_sp();

  address original_pc = nmethod::get_deopt_original_pc(this);
  if (original_pc != NULL) {
    _pc = original_pc;
    _deopt_state = is_deoptimized;
  } else {
    _deopt_state = not_deoptimized;
  }
}

inline bool frame::equal(frame other) const {
  bool ret =  sp() == other.sp()
              && unextended_sp() == other.unextended_sp()
              && fp() == other.fp()
              && pc() == other.pc();
  assert(!ret || ret && cb() == other.cb() && _deopt_state == other._deopt_state, "inconsistent construction");
  return ret;
}

inline intptr_t* frame::id(void) const { return unextended_sp(); }

inline bool frame::is_younger(intptr_t* id) const {
  assert(this->id() != NULL && id != NULL, "NULL frame id");
  return this->id() < id;
}

inline bool frame::is_older(intptr_t* id) const {
  assert(this->id() != NULL && id != NULL, "NULL frame id");
  return this->id() > id;
}

inline address* frame::sender_pc_addr() const {
  return (address*) &(fp()[return_addr_offset]);
}

inline address frame::sender_pc_maybe_signed() const {
  return *sender_pc_addr();
}

inline address frame::sender_pc() const {
  return sender_pc_maybe_signed();
}

inline address* frame::native_param_addr(int idx) const {
  return (address*) addr_at(native_frame_initial_param_offset + idx);
}

inline intptr_t* frame::link() const {
  return (intptr_t*) *(intptr_t**) &(fp()[link_offset]);
}

inline void frame::set_link(intptr_t* addr) {
  *(intptr_t**) &(fp()[link_offset]) = addr;
}

inline intptr_t* frame::unextended_sp() const {
  return _unextended_sp;
}

inline intptr_t* frame::sender_sp() const {
  return addr_at(sender_sp_offset);
}

#ifndef CC_INTERP

inline intptr_t** frame::interpreter_frame_locals_addr() const {
  return (intptr_t**)addr_at(interpreter_frame_locals_offset);
}

inline intptr_t* frame::interpreter_frame_bcx_addr() const {
  return (intptr_t*)addr_at(interpreter_frame_bcx_offset);
}

inline intptr_t* frame::interpreter_frame_mdx_addr() const {
  return (intptr_t*)addr_at(interpreter_frame_mdx_offset);
}

inline ConstantPoolCache** frame::interpreter_frame_cache_addr() const {
  return (ConstantPoolCache**)addr_at(interpreter_frame_cache_offset);
}

inline Method** frame::interpreter_frame_method_addr() const {
  return (Method**)addr_at(interpreter_frame_method_offset);
}

inline intptr_t* frame::interpreter_frame_tos_address() const {
  intptr_t* last_sp = interpreter_frame_last_sp();
  if (last_sp == NULL) {
    return sp();
  } else {
    assert(last_sp <= (intptr_t*) interpreter_frame_monitor_end(), "bad tos");
    return last_sp;
  }
}

inline oop* frame::interpreter_frame_temp_oop_addr() const {
  return (oop *)(fp() + interpreter_frame_oop_temp_offset);
}

#endif // CC_INTERP

inline int frame::pd_oop_map_offset_adjustment() const {
  return 0;
}

inline int frame::interpreter_frame_monitor_size() {
  return BasicObjectLock::size();
}

inline intptr_t* frame::interpreter_frame_expression_stack() const {
  intptr_t* monitor_end = (intptr_t*) interpreter_frame_monitor_end();
  return monitor_end - 1;
}

inline jint frame::interpreter_frame_expression_stack_direction() {
  return -1;
}

inline JavaCallWrapper** frame::entry_frame_call_wrapper_addr() const {
  return (JavaCallWrapper**)addr_at(entry_frame_call_wrapper_offset);
}

inline int frame::local_offset_for_compiler(int local_index, int nof_args, int max_nof_locals, int max_nof_monitors) {
  return (nof_args - local_index + (local_index < nof_args ? 1 : -1));
}

inline int frame::monitor_offset_for_compiler(int local_index, int nof_args, int max_nof_locals, int max_nof_monitors) {
  return local_offset_for_compiler(local_index, nof_args, max_nof_locals, max_nof_monitors);
}

inline int frame::min_local_offset_for_compiler(int nof_args, int max_nof_locals, int max_nof_monitors) {
  return (nof_args - (max_nof_locals + max_nof_monitors * 2) - 1);
}

inline bool frame::volatile_across_calls(Register reg) {
  return true;
}

inline oop frame::saved_oop_result(RegisterMap* map) const {
  oop* result_adr = (oop *)map->location(x10->as_VMReg());
  guarantee(result_adr != NULL, "bad register save location");
  return (*result_adr);
}

inline void frame::set_saved_oop_result(RegisterMap* map, oop obj) {
  oop* result_adr = (oop *)map->location(x10->as_VMReg());
  guarantee(result_adr != NULL, "bad register save location");
  *result_adr = obj;
}

#endif // CPU_RISCV_VM_FRAME_RISCV_INLINE_HPP
