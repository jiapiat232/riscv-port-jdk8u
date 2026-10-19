/*
 * Copyright (c) 2026, Antigravity Porting Project. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"

#ifdef COMPILER1
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "c1/c1_LinearScan.hpp"


// ----------------------------------------------------------------------------
// C1 Stubs & Variables
// ----------------------------------------------------------------------------
int PatchingStub::_patch_info_offset = 0;

void LinearScan::allocate_fpu_stack() { ShouldNotReachHere(); }

void ArrayCopyStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void CounterOverflowStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void DeoptimizeStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void DivByZeroStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void G1PostBarrierStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void G1PreBarrierStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void ImplicitNullCheckStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void MonitorExitStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }
void SimpleExceptionStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }

MonitorEnterStub::MonitorEnterStub(LIR_Opr obj_reg, LIR_Opr lock_reg, CodeEmitInfo* info)
  : MonitorAccessStub(obj_reg, lock_reg), _info(info) {}
void MonitorEnterStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }

RangeCheckStub::RangeCheckStub(CodeEmitInfo* info, LIR_Opr index, bool throw_index_out_of_bounds_exception)
  : _info(info), _index(index), _throw_index_out_of_bounds_exception(throw_index_out_of_bounds_exception) {}
void RangeCheckStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }

PredicateFailedStub::PredicateFailedStub(CodeEmitInfo* info)
  : _info(info) {}
void PredicateFailedStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }

NewInstanceStub::NewInstanceStub(LIR_Opr klass_reg, LIR_Opr result, ciInstanceKlass* klass, CodeEmitInfo* info, Runtime1::StubID stub_id)
  : _klass_reg(klass_reg), _result(result), _klass(klass), _info(info), _stub_id(stub_id) {}
void NewInstanceStub::emit_code(LIR_Assembler* ce) { ShouldNotReachHere(); }

void C1_MacroAssembler::verified_entry() { ShouldNotReachHere(); }
void C1_MacroAssembler::build_frame(int frame_size_in_bytes, int bang_size_in_bytes) { ShouldNotReachHere(); }
#ifndef PRODUCT
void C1_MacroAssembler::verify_stack_oop(int stack_offset) { }
#endif

void Runtime1::initialize_pd() { }
OopMapSet* Runtime1::generate_code_for(Runtime1::StubID id, StubAssembler* sasm) { ShouldNotReachHere(); return NULL; }
const char* Runtime1::pd_name_for_address(address m) { return "pd_stub"; }

LIR_Opr FrameMap::_caller_save_cpu_regs[FrameMap::max_nof_caller_save_cpu_regs];
LIR_Opr FrameMap::_caller_save_fpu_regs[FrameMap::nof_caller_save_fpu_regs];
LIR_Opr FrameMap::fp_opr;
LIR_Opr FrameMap::sp_opr;

void FrameMap::initialize() { }
LIR_Opr FrameMap::map_to_opr(BasicType type, VMRegPair* reg, bool is_two_slot) { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
VMReg FrameMap::fpu_regname(int n) { ShouldNotReachHere(); return VMRegImpl::Bad(); }
LIR_Opr FrameMap::stack_pointer() { return sp_opr; }
bool FrameMap::validate_frame() { return true; }
LIR_Opr FrameMap::method_handle_invoke_SP_save_opr() { return fp_opr; }

void LIRItem::load_byte_item() { ShouldNotReachHere(); }
void LIRItem::load_nonconstant() { ShouldNotReachHere(); }

void LIRGenerator::do_Convert(Convert*) { ShouldNotReachHere(); }
void LIRGenerator::do_LogicOp(LogicOp*) { ShouldNotReachHere(); }
void LIRGenerator::do_ShiftOp(ShiftOp*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::rlock_byte(BasicType) { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::cmp_mem_int(LIR_Condition, LIR_Opr, int, int, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIRGenerator::cmp_reg_mem(LIR_Condition, LIR_Opr, LIR_Opr, int, BasicType, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIRGenerator::do_NegateOp(NegateOp*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::syncTempOpr() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_ArrayCopy(Intrinsic*) { ShouldNotReachHere(); }
void LIRGenerator::do_CheckCast(CheckCast*) { ShouldNotReachHere(); }
void LIRGenerator::do_CompareOp(CompareOp*) { ShouldNotReachHere(); }
void LIRGenerator::do_BlockBegin(BlockBegin*) { ShouldNotReachHere(); }
void LIRGenerator::do_InstanceOf(InstanceOf*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::getThreadTemp() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_MonitorExit(MonitorExit*) { ShouldNotReachHere(); }
void LIRGenerator::do_NewInstance(NewInstance*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::exceptionPcOpr() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::load_immediate(int, BasicType) { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_ArithmeticOp(ArithmeticOp*) { ShouldNotReachHere(); }
void LIRGenerator::do_MonitorEnter(MonitorEnter*) { ShouldNotReachHere(); }
void LIRGenerator::do_NewTypeArray(NewTypeArray*) { ShouldNotReachHere(); }
void LIRGenerator::do_StoreIndexed(StoreIndexed*) { ShouldNotReachHere(); }
void LIRGenerator::do_update_CRC32(Intrinsic*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::exceptionOopOpr() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_MathIntrinsic(Intrinsic*) { ShouldNotReachHere(); }
void LIRGenerator::do_NewMultiArray(NewMultiArray*) { ShouldNotReachHere(); }
LIR_Opr LIRGenerator::getThreadPointer() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_NewObjectArray(NewObjectArray*) { ShouldNotReachHere(); }
void LIRGenerator::trace_block_entry(BlockBegin*) { }
LIR_Opr LIRGenerator::safepoint_poll_register() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::do_UnsafeGetAndSetObject(UnsafeGetAndSetObject*) { ShouldNotReachHere(); }
void LIRGenerator::do_If(If*) { ShouldNotReachHere(); }
bool LIRGenerator::can_store_as_constant(Instruction*, BasicType) const { return false; }
bool LIRGenerator::can_inline_as_constant(Instruction*) const { return false; }
bool LIRGenerator::can_inline_as_constant(LIR_Const*) const { return false; }

LIR_Address* LIRGenerator::generate_address(LIR_Opr base, LIR_Opr index, int shift, int disp, BasicType type) { ShouldNotReachHere(); return NULL; }
void LIRGenerator::do_CompareAndSwap(Intrinsic* x, ValueType* type) { ShouldNotReachHere(); }
void LIRGenerator::get_Object_unsafe(LIR_Opr dest, LIR_Opr src, LIR_Opr offset, BasicType type, bool is_volatile) { ShouldNotReachHere(); }
void LIRGenerator::put_Object_unsafe(LIR_Opr src, LIR_Opr offset, LIR_Opr data, BasicType type, bool is_volatile) { ShouldNotReachHere(); }
void LIRGenerator::increment_counter(LIR_Address* addr, int step) { ShouldNotReachHere(); }
void LIRGenerator::increment_counter(address addr, BasicType type, int step) { ShouldNotReachHere(); }
LIR_Address* LIRGenerator::emit_array_address(LIR_Opr array_opr, LIR_Opr index_opr, BasicType type, bool needs_card_mark) { ShouldNotReachHere(); return NULL; }
LIR_Opr LIRGenerator::result_register_for(ValueType* type, bool callee) { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIRGenerator::volatile_field_load(LIR_Address* address, LIR_Opr result, CodeEmitInfo* info) { ShouldNotReachHere(); }
void LIRGenerator::volatile_field_store(LIR_Opr value, LIR_Address* address, CodeEmitInfo* info) { ShouldNotReachHere(); }
bool LIRGenerator::strength_reduce_multiply(LIR_Opr left, jint constant, LIR_Opr result, LIR_Opr tmp) { return false; }


void LIR_Assembler::align_call(LIR_Code) { ShouldNotReachHere(); }
void LIR_Assembler::breakpoint() { ShouldNotReachHere(); }
void LIR_Assembler::emit_delay(LIR_OpDelay*) { ShouldNotReachHere(); }
void LIR_Assembler::get_thread(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::emit_assert(LIR_OpAssert*) { ShouldNotReachHere(); }
LIR_Opr LIR_Assembler::receiverOpr() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIR_Assembler::vtable_call(LIR_OpJavaCall*) { ShouldNotReachHere(); }
int LIR_Assembler::check_icache() { ShouldNotReachHere(); return 0; }
void LIR_Assembler::emit_opBranch(LIR_OpBranch*) { ShouldNotReachHere(); }
void LIR_Assembler::set_24bit_FPU() { ShouldNotReachHere(); }
void LIR_Assembler::emit_alloc_obj(LIR_OpAllocObj*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_opConvert(LIR_OpConvert*) { ShouldNotReachHere(); }
void LIR_Assembler::membar_acquire() { ShouldNotReachHere(); }
void LIR_Assembler::membar_release() { ShouldNotReachHere(); }
void LIR_Assembler::membar_loadload() { ShouldNotReachHere(); }
void LIR_Assembler::emit_alloc_array(LIR_OpAllocArray*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_opTypeCheck(LIR_OpTypeCheck*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_updatecrc32(LIR_OpUpdateCRC32*) { ShouldNotReachHere(); }
void LIR_Assembler::membar_loadstore() { ShouldNotReachHere(); }
void LIR_Assembler::membar_storeload() { ShouldNotReachHere(); }
LIR_Opr LIR_Assembler::osrBufferPointer() { ShouldNotReachHere(); return LIR_OprFact::illegalOpr; }
void LIR_Assembler::emit_profile_call(LIR_OpProfileCall*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_profile_type(LIR_OpProfileType*) { ShouldNotReachHere(); }
void LIR_Assembler::membar_storestore() { ShouldNotReachHere(); }
int LIR_Assembler::emit_deopt_handler() { ShouldNotReachHere(); return 0; }
int LIR_Assembler::emit_unwind_handler() { ShouldNotReachHere(); return 0; }
void LIR_Assembler::emit_compare_and_swap(LIR_OpCompareAndSwap*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_static_call_stub() { ShouldNotReachHere(); }
int LIR_Assembler::emit_exception_handler() { ShouldNotReachHere(); return 0; }

void LIR_Assembler::align_backward_branch_target() { }
void LIR_Assembler::fld(int) { ShouldNotReachHere(); }
void LIR_Assembler::pop(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::fpop() { ShouldNotReachHere(); }
void LIR_Assembler::fxch(int) { ShouldNotReachHere(); }
void LIR_Assembler::push(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::ffree(int) { ShouldNotReachHere(); }
void LIR_Assembler::membar() { ShouldNotReachHere(); }
void LIR_Assembler::ic_call(LIR_OpJavaCall*) { ShouldNotReachHere(); }
void LIR_Assembler::emit_op3(LIR_Op3*) { ShouldNotReachHere(); }
void LIR_Assembler::peephole(LIR_List*) { }
void LIR_Assembler::emit_lock(LIR_OpLock*) { ShouldNotReachHere(); }
void LIR_Assembler::osr_entry() { ShouldNotReachHere(); }
void LIR_Assembler::prefetchr(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::prefetchw(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::reset_FPU() { ShouldNotReachHere(); }
void LIR_Assembler::return_op(LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::unwind_op(LIR_Opr) { ShouldNotReachHere(); }
int LIR_Assembler::initial_frame_size_in_bytes() const { return 0; }
void LIR_Assembler::rt_call(LIR_Opr, address, const LIR_OprList*, LIR_Opr, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIR_Assembler::arith_op(LIR_Code, LIR_Opr, LIR_Opr, LIR_Opr, CodeEmitInfo*, bool) { ShouldNotReachHere(); }
void LIR_Assembler::logic_op(LIR_Code, LIR_Opr, LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::shift_op(LIR_Code, LIR_Opr, LIR_Opr, LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::shift_op(LIR_Code, LIR_Opr, int, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::throw_op(LIR_Opr, LIR_Opr, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIR_Assembler::atomic_op(LIR_Code, LIR_Opr, LIR_Opr, LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::comp_fl2i(LIR_Code, LIR_Opr, LIR_Opr, LIR_Opr, LIR_Op2*) { ShouldNotReachHere(); }
void LIR_Assembler::const2mem(LIR_Opr, LIR_Opr, BasicType, CodeEmitInfo*, bool) { ShouldNotReachHere(); }
void LIR_Assembler::const2reg(LIR_Opr, LIR_Opr, LIR_PatchCode, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIR_Assembler::reg2stack(LIR_Opr, LIR_Opr, BasicType, bool) { ShouldNotReachHere(); }
void LIR_Assembler::stack2reg(LIR_Opr, LIR_Opr, BasicType) { ShouldNotReachHere(); }
void LIR_Assembler::stack2stack(LIR_Opr, LIR_Opr, BasicType) { ShouldNotReachHere(); }
void LIR_Assembler::reg2reg(LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::reg2mem(LIR_Opr, LIR_Opr, BasicType, LIR_PatchCode, CodeEmitInfo*, bool, bool, bool) { ShouldNotReachHere(); }
void LIR_Assembler::mem2reg(LIR_Opr, LIR_Opr, BasicType, LIR_PatchCode, CodeEmitInfo*, bool, bool) { ShouldNotReachHere(); }
int LIR_Assembler::safepoint_poll(LIR_Opr, CodeEmitInfo*) { ShouldNotReachHere(); return 0; }
void LIR_Assembler::volatile_move_op(LIR_Opr, LIR_Opr, BasicType, CodeEmitInfo*) { ShouldNotReachHere(); }
void LIR_Assembler::leal(LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::negate(LIR_Opr, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::monitor_address(int, LIR_Opr) { ShouldNotReachHere(); }
void LIR_Assembler::const2stack(LIR_Opr src, LIR_Opr dest) { ShouldNotReachHere(); }
void LIR_Assembler::intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op) { ShouldNotReachHere(); }
void LIR_Assembler::call(LIR_OpJavaCall* op, relocInfo::relocType rtype) { ShouldNotReachHere(); }
void LIR_Assembler::cmove(LIR_Condition code, LIR_Opr left, LIR_Opr right, LIR_Opr result, BasicType type) { ShouldNotReachHere(); }
void LIR_Assembler::comp_op(LIR_Condition condition, LIR_Opr src, LIR_Opr result, LIR_Op2* op) { ShouldNotReachHere(); }


#endif // COMPILER1

#ifdef COMPILER2
#include "opto/matcher.hpp"
#include "opto/machnode.hpp"
#include "opto/runtime.hpp"
#include "opto/compile.hpp"
#include "opto/locknode.hpp"

// ----------------------------------------------------------------------------
// C2 Stubs & Matcher
// ----------------------------------------------------------------------------
const bool Matcher::int_in_long = true;
const int Matcher::init_array_short_size = 0;
const bool Matcher::misaligned_doubles_ok = true;
const bool Matcher::clone_shift_expressions = false;
const bool Matcher::need_masked_shift_count = false;
const bool Matcher::require_postalloc_expand = false;
const bool Matcher::init_array_count_is_in_bytes = true;
const bool Matcher::rematerialize_float_constants = false;
const bool Matcher::strict_fp_requires_explicit_rounding = false;

bool Matcher::can_be_java_arg(int reg) { return false; }
bool Matcher::is_spillable_arg(int reg) { return false; }
bool Matcher::use_asm_for_ldiv_by_con(jlong divisor) { return false; }
RegMask Matcher::divI_proj_mask() { ShouldNotReachHere(); return RegMask(); }
RegMask Matcher::modI_proj_mask() { ShouldNotReachHere(); return RegMask(); }
RegMask Matcher::divL_proj_mask() { ShouldNotReachHere(); return RegMask(); }
RegMask Matcher::modL_proj_mask() { ShouldNotReachHere(); return RegMask(); }
const RegMask Matcher::method_handle_invoke_SP_save_mask() { return RegMask(); }
bool Matcher::is_short_branch_offset(int rule, int br_size, int offset) { return false; }
const int Matcher::vector_width_in_bytes(BasicType bt) { return 0; }
const int Matcher::max_vector_size(const BasicType bt) { return 0; }
const int Matcher::min_vector_size(const BasicType bt) { return 0; }
const uint Matcher::vector_ideal_reg(int len) { return 0; }
const bool Matcher::match_rule_supported(int opcode) { return false; }
int Matcher::regnum_to_fpu_offset(int regnum) { return 0; }
const bool Matcher::misaligned_vectors_ok() { return false; }
const bool Matcher::pass_original_key_for_aes() { return false; }
const uint Matcher::vector_shift_count_ideal_reg(int len) { return 0; }
bool Matcher::narrow_oop_use_complex_address() { return false; }
bool Matcher::narrow_klass_use_complex_address() { return false; }
const bool Matcher::convL2FSupported() { return true; }
const int Matcher::float_cmove_cost() { return 0; }
const int Matcher::long_cmove_cost() { return 0; }
bool Matcher::float_in_double() { return false; }
const bool Matcher::isSimpleConstant64(jlong value) { return false; }


const RegMask& MachConstantBaseNode::_out_RegMask = RegMask::Empty;
int Compile::ConstantTable::calculate_table_base_offset() const { return 0; }
bool SafePointNode::needs_polling_address_input() { return false; }
void OptoRuntime::generate_exception_blob() { }
int MachCallRuntimeNode::ret_addr_offset() { return 0; }
int MachCallStaticJavaNode::ret_addr_offset() { return 0; }

void BoxLockNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint BoxLockNode::size(PhaseRegAlloc* ra_) const { return 4; }
#ifndef PRODUCT
void BoxLockNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachNopNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachNopNode::size(PhaseRegAlloc* ra_) const { return 4; }
const Pipeline* MachNopNode::pipeline() const { return NULL; }
#ifndef PRODUCT
void MachNopNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachUEPNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachUEPNode::size(PhaseRegAlloc* ra_) const { return 4; }
#ifndef PRODUCT
void MachUEPNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachPrologNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachPrologNode::size(PhaseRegAlloc* ra_) const { return 4; }
int MachPrologNode::reloc() const { return 0; }
#ifndef PRODUCT
void MachPrologNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachEpilogNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachEpilogNode::size(PhaseRegAlloc* ra_) const { return 4; }
int MachEpilogNode::reloc() const { return 0; }
const Pipeline* MachEpilogNode::pipeline() const { return NULL; }
int MachEpilogNode::safepoint_offset() const { return 0; }
#ifndef PRODUCT
void MachEpilogNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachSpillCopyNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachSpillCopyNode::size(PhaseRegAlloc* ra_) const { return 4; }
#ifndef PRODUCT
void MachSpillCopyNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

void MachBreakpointNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachBreakpointNode::size(PhaseRegAlloc* ra_) const { return 4; }
#ifndef PRODUCT
void MachBreakpointNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

bool MachConstantBaseNode::requires_postalloc_expand() const { return false; }
void MachConstantBaseNode::postalloc_expand(GrowableArray<Node*>* nodes, PhaseRegAlloc* ra_) { ShouldNotReachHere(); }
void MachConstantBaseNode::emit(CodeBuffer& cbuf, PhaseRegAlloc* ra_) const { ShouldNotReachHere(); }
uint MachConstantBaseNode::size(PhaseRegAlloc* ra_) const { return 4; }
#ifndef PRODUCT
void MachConstantBaseNode::format(PhaseRegAlloc* ra_, outputStream* st) const { }
#endif

#endif // COMPILER2
