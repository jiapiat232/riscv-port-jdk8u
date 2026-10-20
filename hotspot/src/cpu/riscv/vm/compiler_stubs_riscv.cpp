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
void LinearScan::allocate_fpu_stack() { ShouldNotReachHere(); }

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
