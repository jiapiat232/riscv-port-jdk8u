/*
 * Copyright (c) 2003, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_RISCV_VM_INTERP_MASM_RISCV_HPP
#define CPU_RISCV_VM_INTERP_MASM_RISCV_HPP

#include "asm/macroAssembler.hpp"
#include "interpreter/invocationCounter.hpp"
#include "runtime/frame.hpp"

// This file specializes the assembler with interpreter-specific macros
class InterpreterMacroAssembler: public MacroAssembler {
#ifndef CC_INTERP
 protected:
  virtual void call_VM_leaf_base(address entry_point,
                                 int number_of_arguments);

  virtual void call_VM_base(Register oop_result,
                            Register java_thread,
                            Register last_java_sp,
                            address  entry_point,
                            int number_of_arguments,
                            bool check_exceptions);

  virtual void check_and_handle_popframe(Register java_thread);
  virtual void check_and_handle_earlyret(Register java_thread);

  // Base routine for all dispatches
  void dispatch_base(TosState state, address* table, bool verifyoop = true);
#endif // CC_INTERP

 public:
  InterpreterMacroAssembler(CodeBuffer* code) : MacroAssembler(code) {}

  void load_earlyret_value(TosState state);

#ifdef CC_INTERP
  void save_bcp() {}
  void restore_bcp() {}
  void get_method(Register reg);
#else
  // Interpreter-specific registers
  void save_bcp() {
    sd(xbcp, Address(fp, frame::interpreter_frame_bcx_offset * wordSize));
  }

  void restore_bcp() {
    ld(xbcp, Address(fp, frame::interpreter_frame_bcx_offset * wordSize));
  }

  void restore_locals() {
    ld(xlocals, Address(fp, frame::interpreter_frame_locals_offset * wordSize));
  }

  void restore_constant_pool_cache() {
    ld(xcpool, Address(fp, frame::interpreter_frame_cache_offset * wordSize));
  }

  // Helpers for runtime call arguments/results
  void get_method(Register reg) {
    ld(reg, Address(fp, frame::interpreter_frame_method_offset * wordSize));
  }

  void get_const(Register reg) {
    get_method(reg);
    ld(reg, Address(reg, in_bytes(Method::const_offset())));
  }

  void get_constant_pool(Register reg) {
    get_const(reg);
    ld(reg, Address(reg, in_bytes(ConstMethod::constants_offset())));
  }

  void get_constant_pool_cache(Register reg) {
    get_constant_pool(reg);
    ld(reg, Address(reg, ConstantPool::cache_offset_in_bytes()));
  }

  void get_cpool_and_tags(Register cpool, Register tags) {
    get_constant_pool(cpool);
    ld(tags, Address(cpool, ConstantPool::tags_offset_in_bytes()));
  }

  void get_unsigned_2_byte_index_at_bcp(Register reg, int bcp_offset);
  void get_cache_and_index_at_bcp(Register cache, Register index, int bcp_offset, size_t index_size = sizeof(u2));
  void get_cache_and_index_and_bytecode_at_bcp(Register cache, Register index, Register bytecode, int byte_no, int bcp_offset, size_t index_size = sizeof(u2));
  void get_cache_entry_pointer_at_bcp(Register cache, Register tmp, int bcp_offset, size_t index_size = sizeof(u2));
  void get_cache_index_at_bcp(Register index, int bcp_offset, size_t index_size = sizeof(u2));
  void get_method_counters(Register method, Register mcs, Label& skip);

  // Load cpool->resolved_references(index)
  void load_resolved_reference_at_index(Register result, Register index);

  // Expression stack operations
  void pop_ptr(Register r = x10);
  void pop_i(Register r = x10);
  void pop_l(Register r = x10);
  void pop_f(FloatRegister r = f10);
  void pop_d(FloatRegister r = f10);
  void push_ptr(Register r = x10);
  void push_i(Register r = x10);
  void push_l(Register r = x10);
  void push_f(FloatRegister r = f10);
  void push_d(FloatRegister r = f10);

  void pop(Register r)  { ((MacroAssembler*)this)->pop(r); }
  void push(Register r) { ((MacroAssembler*)this)->push(r); }

  void pop(TosState state);  // transition vtos -> state
  void push(TosState state); // transition state -> vtos

  void empty_expression_stack() {
    ld(esp, Address(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize));
    sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  }

  // Helpers for swap and dup
  void load_ptr(int n, Register val);
  void store_ptr(int n, Register val);

  void prepare_to_jump_from_interpreted();
  void jump_from_interpreted(Register method, Register temp = t0);

  void verify_field_offset(Register reg);
  void get_vm_result_2(Register metadata_result, Register java_thread) {
    ((MacroAssembler*)this)->get_vm_result_2(metadata_result, java_thread);
  }

  void narrow(Register result);

  void verify_FPU(int stack_depth, TosState state = ftos) {}
  void verify_method_data_pointer() {}
  void dispatch_prolog(TosState state, int step = 0) {}
  void dispatch_epilog(TosState state, int step = 0);
  void get_dispatch();
  void dispatch_only(TosState state);
  void dispatch_only_normal(TosState state);
  void dispatch_only_noverify(TosState state);
  void dispatch_next(TosState state, int step = 0);
  void dispatch_via(TosState state, address* table);

  // Synchronization
  void lock_object(Register lock_reg);
  void unlock_object(Register lock_reg);

  // Stack activation removal
  void remove_activation(TosState state,
                         bool throw_monitor_exception = true,
                         bool install_monitor_exception = true,
                         bool notify_jvmdi = true);

  // Profile and counters
  void update_mdp_for_ret(Register return_bci);
  void set_method_data_pointer_for_bcp();
  void test_method_data_pointer(Register mdp, Label& zero_continue);
  void increment_mask_and_jump(Address counter_addr,
                               int increment, Address mask,
                               Register tmp1, Register tmp2,
                               bool preloaded, Label* where);
  void increment_mask_and_jump(Address counter_addr,
                               int increment, int mask,
                               Register tmp1, Register tmp2,
                               bool preloaded, Label* where);

  void gen_subtype_check(Register sub_klass, Label& ok_is_subtype) {
    check_klass_subtype(sub_klass, x10, x12, ok_is_subtype);
  }

  void profile_taken_branch(Register mdp) {}
  void profile_not_taken_branch(Register mdp) {}
  void profile_call(Register mdp) {}
  void profile_final_call(Register mdp) {}
  void profile_virtual_call(Register receiver, Register mdp, Register scratch2 = noreg, bool receiver_can_be_null = false) {}
  void profile_ret(Register return_bci, Register mdp) {}
  void profile_null_seen(Register mdp) {}
  void profile_typecheck(Register mdp, Register klass, Register scratch = noreg) {}
  void profile_typecheck_failed(Register mdp) {}
  void profile_switch_default(Register mdp) {}
  void profile_switch_case(Register index_in_scratch, Register mdp, Register scratch2) {}
  void profile_obj_type(Register obj, const Address& mdo_addr) {}
  void profile_arguments_type(Register mdp, Register callee, Register tmp, bool is_virtual) {}
  void profile_return_type(Register mdp, Register ret, Register tmp) {}
  void profile_parameters_type(Register mdp, Register tmp1, Register tmp2) {}

  typedef enum { NotifyJVMTI, SkipNotifyJVMTI } NotifyMethodExitMode;
  void notify_method_entry();
  void notify_method_exit(TosState state, NotifyMethodExitMode mode);
  void verify_frame_setup();
#endif // !CC_INTERP
};

#endif // CPU_RISCV_VM_INTERP_MASM_RISCV_HPP
