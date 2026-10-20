/*
 * Copyright (c) 2003, 2026, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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

#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "assembler_riscv.hpp"
#include "code/vtableStubs.hpp"
#include "interp_masm_riscv.hpp"
#include "memory/resourceArea.hpp"
#include "oops/compiledICHolder.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klassVtable.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_riscv.inline.hpp"
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif

#define __ masm->

#ifndef PRODUCT
extern "C" void bad_compiled_vtable_index(JavaThread* thread, oop receiver, int index);
#endif

VtableStub* VtableStubs::create_vtable_stub(int vtable_index) {
  const int stub_code_length = VtableStub::pd_code_size_limit(true);
  VtableStub* s = new(stub_code_length) VtableStub(true, vtable_index);
  if (s == NULL) {
    return NULL;
  }

  ResourceMark rm;
  CodeBuffer cb(s->entry_point(), stub_code_length);
  MacroAssembler* masm = new MacroAssembler(&cb);

#ifndef PRODUCT
  if (CountCompiledCalls) {
    __ la(t2, ExternalAddress((address) SharedRuntime::nof_megamorphic_calls_addr()));
    __ lw(t0, Address(t2, 0));
    __ addiw(t0, t0, 1);
    __ sw(t0, Address(t2, 0));
  }
#endif

  assert(VtableStub::receiver_location() == j_rarg0->as_VMReg(), "receiver expected in j_rarg0");

  // get receiver klass
  address npe_addr = __ pc();
  __ load_klass(t2, j_rarg0);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    // check offset vs vtable length
    __ lwu(t0, Address(t2, InstanceKlass::vtable_length_offset() * wordSize));
    __ li(t1, vtable_index * vtableEntry::size());
    __ bgt(t0, t1, L);
    __ enter();
    __ li(x12, vtable_index);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, bad_compiled_vtable_index), j_rarg0, x12);
    __ leave();
    __ bind(L);
  }
#endif // PRODUCT

  __ lookup_virtual_method(t2, vtable_index, xmethod);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    __ beqz(xmethod, L);
    __ ld(t0, Address(xmethod, Method::from_compiled_offset()));
    __ bnez(t0, L);
    __ stop("Vtable entry is NULL");
    __ bind(L);
  }
#endif // PRODUCT

  address ame_addr = __ pc();
  __ ld(t0, Address(xmethod, Method::from_compiled_offset()));
  __ jr(t0);

  masm->flush();

  if (PrintMiscellaneous && (WizardMode || Verbose)) {
    tty->print_cr("vtable #%d at " PTR_FORMAT "[%d] left over: %d",
                  vtable_index, p2i(s->entry_point()),
                  (int)(s->code_end() - s->entry_point()),
                  (int)(s->code_end() - __ pc()));
  }
  guarantee(__ pc() <= s->code_end(), "overflowed buffer");

  s->set_exception_points(npe_addr, ame_addr);
  return s;
}

VtableStub* VtableStubs::create_itable_stub(int itable_index) {
  const int stub_code_length = VtableStub::pd_code_size_limit(false);
  VtableStub* s = new(stub_code_length) VtableStub(false, itable_index);
  if (s == NULL) {
    return NULL;
  }

  ResourceMark rm;
  CodeBuffer cb(s->entry_point(), stub_code_length);
  MacroAssembler* masm = new MacroAssembler(&cb);

#ifndef PRODUCT
  if (CountCompiledCalls) {
    __ la(t2, ExternalAddress((address) SharedRuntime::nof_megamorphic_calls_addr()));
    __ lw(t0, Address(t2, 0));
    __ addiw(t0, t0, 1);
    __ sw(t0, Address(t2, 0));
  }
#endif

  assert(VtableStub::receiver_location() == j_rarg0->as_VMReg(), "receiver expected in j_rarg0");

  // NOTE: VtableStubs::create_itable_stub is called from BOTH C2 (which has no
  // callee-saved registers) AND C1 (where x9, x18..x22, x24..x26 ARE live
  // callee-saved registers allocated across call sites!).
  // Therefore we MUST NOT clobber x18/x19 here; use caller-saved temporaries
  // t2 (x7), x28 (t3), x29 (t4), x30 (t5) instead, matching AArch64's r10/r11/r16!
  const Register recv_klass_reg     = t2;      // x7 (caller-saved)
  const Register holder_klass_reg   = x30;     // t5 (caller-saved)
  const Register resolved_klass_reg = xmethod; // x31 (t6, caller-saved)
  const Register temp_reg           = x28;     // t3 (caller-saved)
  const Register temp_reg2          = x29;     // t4 (caller-saved)
  const Register icholder_reg       = xmethod;

  Label L_no_such_interface;

  __ ld(holder_klass_reg,   Address(icholder_reg, CompiledICHolder::holder_metadata_offset()));
  __ ld(resolved_klass_reg, Address(icholder_reg, CompiledICHolder::holder_klass_offset()));

  address npe_addr = __ pc();
  __ load_klass(recv_klass_reg, j_rarg0);

  __ lookup_interface_method(recv_klass_reg, resolved_klass_reg, noreg,
                             temp_reg2, temp_reg,
                             L_no_such_interface,
                             /*return_method=*/false);

  __ lookup_interface_method(recv_klass_reg, holder_klass_reg, itable_index,
                             xmethod, temp_reg,
                             L_no_such_interface);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L2;
    __ beqz(xmethod, L2);
    __ ld(t0, Address(xmethod, Method::from_compiled_offset()));
    __ bnez(t0, L2);
    __ stop("compiler entrypoint is null");
    __ bind(L2);
  }
#endif // PRODUCT

  address ame_addr = __ pc();
  __ ld(t0, Address(xmethod, Method::from_compiled_offset()));
  __ jr(t0);

  __ bind(L_no_such_interface);
  __ far_jump(RuntimeAddress(StubRoutines::throw_IncompatibleClassChangeError_entry()));

  masm->flush();

  if (PrintMiscellaneous && (WizardMode || Verbose)) {
    tty->print_cr("itable #%d at " PTR_FORMAT "[%d] left over: %d",
                  itable_index, p2i(s->entry_point()),
                  (int)(s->code_end() - s->entry_point()),
                  (int)(s->code_end() - __ pc()));
  }
  guarantee(__ pc() <= s->code_end(), "overflowed buffer");

  s->set_exception_points(npe_addr, ame_addr);
  return s;
}

int VtableStub::pd_code_size_limit(bool is_vtable_stub) {
  if (DebugVtables || CountCompiledCalls || VerifyOops) {
    return 1024;
  }
  return is_vtable_stub ? 128 : 256;
}

int VtableStub::pd_code_alignment() {
  return wordSize;
}
