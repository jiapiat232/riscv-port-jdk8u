/*
 * Copyright (c) 2000, 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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

#ifndef CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP
#define CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP

// ArrayCopyStub needs access to bailout
friend class ArrayCopyStub;

private:

  // arith_op sub functions
  void arith_op_single_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest);
  void arith_op_double_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest);
  void arith_op_single_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest);
  void arith_op_double_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest);
  void arith_op_single_cpu_right_constant(LIR_Code code, LIR_Opr left, LIR_Opr right, Register lreg, Register dreg);
  void arithmetic_idiv(LIR_Op3* op, bool is_irem);

  // arraycopy sub functions
  void generic_arraycopy(Register src, Register src_pos, Register length,
                         Register dst, Register dst_pos, CodeStub *stub);
  void arraycopy_simple_check(Register src, Register src_pos, Register length,
                              Register dst, Register dst_pos, Register tmp,
                              CodeStub *stub, int flags);
  void arraycopy_checkcast(Register src, Register src_pos, Register length,
                           Register dst, Register dst_pos, Register tmp,
                           CodeStub *stub, BasicType basic_type,
                           address copyfunc_addr, int flags);
  void arraycopy_type_check(Register src, Register src_pos, Register length,
                            Register dst, Register dst_pos, Register tmp,
                            CodeStub *stub, BasicType basic_type, int flags);
  void arraycopy_assert(Register src, Register dst, Register tmp, ciArrayKlass *default_type, int flags);
  void arraycopy_prepare_params(Register src, Register src_pos, Register length,
                                Register dst, Register dst_pos, BasicType basic_type);
  void arraycopy_checkcast_prepare_params(Register src, Register src_pos, Register length,
                                          Register dst, Register dst_pos, BasicType basic_type);
  void arraycopy_store_args(Register src, Register src_pos, Register length,
                            Register dst, Register dst_pos);
  void arraycopy_load_args(Register src, Register src_pos, Register length,
                           Register dst, Register dst_pos);

  int array_element_size(BasicType type) const;

  void arith_fpu_implementation(LIR_Code code, int left_index, int right_index, int dest_index, bool pop_fpu_stack);

  static Register as_reg(LIR_Opr op) {
    return op->is_double_cpu() ? op->as_register_lo() : op->as_register();
  }

  Address as_Address(LIR_Address* addr, Register tmp);

  // helper functions which checks for overflow and sets bailout if it
  // occurs.  Always returns a valid embeddable pointer but in the
  // bailout case the pointer won't be to unique storage.
  address float_constant(float f);
  address double_constant(double d);
  address int_constant(jlong n);

  bool is_literal_address(LIR_Address* addr);

  // Ensure we have a valid Address (base + offset) to a stack-slot.
  Address stack_slot_address(int index, uint shift, int adjust = 0);

  // Record the type of the receiver in ReceiverTypeData
  void type_profile_helper(Register mdo,
                           ciMethodData *md, ciProfileData *data,
                           Register recv, Label* update_done);

  void add_debug_info_for_branch(address adr, CodeEmitInfo* info);

  void casw(Register addr, Register newval, Register cmpval);
  void caswu(Register addr, Register newval, Register cmpval);
  void casl(Register addr, Register newval, Register cmpval);

  void poll_for_safepoint(relocInfo::relocType rtype, CodeEmitInfo* info = NULL);

  void deoptimize_trap(CodeEmitInfo *info);

  void check_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp,
                      Label &next, Label &none, Address mdo_addr);
  void check_no_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp, Address mdo_addr, Label &next);

  void check_exact_klass(Register tmp, ciKlass* exact_klass);

  void check_null(Register tmp, Label &update, intptr_t current_klass, Address mdo_addr, bool do_update, Label &next);

  void (MacroAssembler::*add)(Register prev, RegisterOrConstant incr, Register addr);
  void (MacroAssembler::*xchg)(Register prev, Register newv, Register addr);

  void get_op(BasicType type);

  // emit_typecheck_helper sub functions
  void data_check(LIR_OpTypeCheck *op, ciMethodData **md, ciProfileData **data);
  void typecheck_helper_slowcheck(ciKlass* k, Register obj, Register Rtmp1,
                                  Register k_RInfo, Register klass_RInfo,
                                  Label* failure_target, Label* success_target);
  void profile_object(ciMethodData* md, ciProfileData* data, Register obj,
                      Register klass_RInfo, Label* obj_is_null);
  void typecheck_loaded(LIR_OpTypeCheck* op, ciKlass* k, Register k_RInfo);

  // emit_opTypeCheck sub functions
  void typecheck_lir_store(LIR_OpTypeCheck* op, bool should_profile);

  void type_profile(Register obj, ciMethodData* md, Register klass_RInfo, Register k_RInfo,
                    ciProfileData* data, Label* success, Label* failure,
                    Label& profile_cast_success, Label& profile_cast_failure);

  void lir_store_slowcheck(Register k_RInfo, Register klass_RInfo, Register Rtmp1,
                           Label* success_target, Label* failure_target);

  void const2reg_helper(LIR_Opr src);

  void emit_branch(LIR_Condition cmp_flag, LIR_Opr cmp1, LIR_Opr cmp2, Label& label, bool is_far, bool is_unordered);

  void logic_op_reg32(Register dst, Register left, Register right, LIR_Code code);
  void logic_op_reg(Register dst, Register left, Register right, LIR_Code code);
  void logic_op_imm(Register dst, Register left, int right, LIR_Code code);

public:

  enum {
    // See emit_static_call_stub for detail
    // CompiledStaticCall::to_interp_stub_size() (14) + CompiledStaticCall::to_trampoline_stub_size() (1 + 3 + address)
    _call_stub_size = 14 * NativeInstruction::instruction_size +
                      (NativeInstruction::instruction_size + NativeCallTrampolineStub::instruction_size),
    call_stub_size = _call_stub_size,
    // See emit_exception_handler for detail
    // verify_not_null_oop + far_call + should_not_reach_here + invalidate_registers(DEBUG_ONLY)
    _exception_handler_size = DEBUG_ONLY(1*K) NOT_DEBUG(548),
    exception_handler_size = _exception_handler_size,
    // See emit_deopt_handler for detail
    // auipc (1) + far_jump (6 or 2)
    _deopt_handler_size = 1 * NativeInstruction::instruction_size +
                          6 * NativeInstruction::instruction_size,
    deopt_handler_size = _deopt_handler_size
  };

  void cmove(LIR_Condition code, LIR_Opr left, LIR_Opr right, LIR_Opr result, BasicType type,
             LIR_Opr cmp_opr1, LIR_Opr cmp_opr2);

  void store_parameter(Register r, int offset_from_rsp_in_words);
  void store_parameter(jint c,     int offset_from_rsp_in_words);
  void store_parameter(jobject c,  int offset_from_rsp_in_words);

#endif // CPU_RISCV_VM_C1_LIRASSEMBLER_RISCV_HPP
