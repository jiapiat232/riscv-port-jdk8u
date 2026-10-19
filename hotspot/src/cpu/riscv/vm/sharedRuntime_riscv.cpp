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

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "code/compiledIC.hpp"
#include "code/icBuffer.hpp"
#include "code/nmethod.hpp"
#include "code/scopeDesc.hpp"
#include "code/vtableStubs.hpp"
#include "interpreter/interpreter.hpp"
#include "interp_masm_riscv.hpp"
#include "memory/resourceArea.hpp"
#include "oops/compiledICHolder.hpp"
#include "oops/method.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframeArray.hpp"
#include "vmreg_riscv.inline.hpp"
#ifdef COMPILER1
#include "c1/c1_Runtime1.hpp"
#endif
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif

#define __ masm->

// -------------------------------------------------------------
// RegisterSaver: saves/restores registers for Safepoint & Deopt
// -------------------------------------------------------------
class RegisterSaver {
 public:
  enum {
    int_registers   = 32,
    float_registers = 32,
    reg_save_words  = int_registers + float_registers + 2 // + fp, ra
  };

  static OopMap* save_live_registers(MacroAssembler* masm, int additional_frame_words, int* total_frame_words, bool save_vectors = false);
  static void restore_live_registers(MacroAssembler* masm, bool restore_vectors = false);

  static int reg_offset_in_bytes(Register r) {
    assert(r != sp && r != fp && r != ra, "special register offset");
    return (r->encoding() - 1) * wordSize;
  }
};

OopMap* RegisterSaver::save_live_registers(MacroAssembler* masm, int additional_frame_words, int* total_frame_words, bool save_vectors) {
  int frame_words = reg_save_words + additional_frame_words;
  frame_words = round_to(frame_words, 2); // 16-byte aligned
  *total_frame_words = frame_words;

  OopMap* map = new OopMap(frame_words, 0);

  __ addi(sp, sp, -frame_words * wordSize);
  __ sd(ra, Address(sp, (frame_words - 1) * wordSize));
  __ sd(fp, Address(sp, (frame_words - 2) * wordSize));
  __ addi(fp, sp, frame_words * wordSize);

  // Save integer registers: x1..x31
  for (int i = 1; i < 32; i++) {
    Register r = as_Register(i);
    if (r != sp && r != fp && r != ra) {
      __ sd(r, Address(sp, (i - 1) * wordSize));
      map->set_callee_saved(VMRegImpl::stack2reg(i - 1), r->as_VMReg());
    }
  }

  // Save floating point registers: f0..f31
  int f_base = 32;
  for (int i = 0; i < 32; i++) {
    FloatRegister f = as_FloatRegister(i);
    __ fsd(f, Address(sp, (f_base + i) * wordSize));
    map->set_callee_saved(VMRegImpl::stack2reg(f_base + i), f->as_VMReg());
  }

  return map;
}

void RegisterSaver::restore_live_registers(MacroAssembler* masm, bool restore_vectors) {
  // Restore floating point registers: f0..f31
  int f_base = 32;
  for (int i = 0; i < 32; i++) {
    FloatRegister f = as_FloatRegister(i);
    __ fld(f, Address(sp, (f_base + i) * wordSize));
  }

  // Restore integer registers: x1..x31
  for (int i = 1; i < 32; i++) {
    Register r = as_Register(i);
    if (r != sp && r != fp && r != ra) {
      __ ld(r, Address(sp, (i - 1) * wordSize));
    }
  }

  // Restore fp and ra, and unwind stack frame back to caller sp.
  // Note: save_live_registers sets fp = caller_sp.
  // Address(fp, -1 * wordSize) is caller_ra, Address(fp, -2 * wordSize) is caller_fp.
  // We use the signal-safe sequence (matching upstream leave() semantics):
  // move sp to the save area, load ra and fp, then advance sp to caller_sp.
  __ addi(sp, fp, -2 * wordSize);
  __ ld(ra, Address(sp, wordSize));
  __ ld(fp, Address(sp, 0));
  __ addi(sp, sp, 2 * wordSize);
}

// -------------------------------------------------------------
// Calling Conventions
// -------------------------------------------------------------
bool SharedRuntime::is_wide_vector(int size) {
  return false;
}

uint SharedRuntime::out_preserve_stack_slots() {
  return 0;
}

int SharedRuntime::java_calling_convention(const BasicType *sig_bt,
                                           VMRegPair *regs,
                                           int total_args_passed,
                                           int is_outgoing) {
  static const Register INT_ArgReg[Argument::n_int_register_parameters_j] = {
    j_rarg0, j_rarg1, j_rarg2, j_rarg3, j_rarg4, j_rarg5, j_rarg6, j_rarg7
  };
  static const FloatRegister FP_ArgReg[Argument::n_float_register_parameters_j] = {
    j_farg0, j_farg1, j_farg2, j_farg3, j_farg4, j_farg5, j_farg6, j_farg7
  };

  uint int_args = 0;
  uint fp_args = 0;
  uint stk_args = 0; // 64-bit slot = 2 VMReg stack units

  for (int i = 0; i < total_args_passed; i++) {
    switch (sig_bt[i]) {
    case T_BOOLEAN:
    case T_CHAR:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
      if (int_args < Argument::n_int_register_parameters_j) {
        regs[i].set1(INT_ArgReg[int_args++]->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_VOID:
      assert(i != 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "expecting half");
      regs[i].set_bad();
      break;
    case T_LONG:
      assert(sig_bt[i + 1] == T_VOID, "expecting half");
      // fall through
    case T_OBJECT:
    case T_ARRAY:
    case T_ADDRESS:
      if (int_args < Argument::n_int_register_parameters_j) {
        regs[i].set2(INT_ArgReg[int_args++]->as_VMReg());
      } else {
        regs[i].set2(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_FLOAT:
      if (fp_args < Argument::n_float_register_parameters_j) {
        regs[i].set1(FP_ArgReg[fp_args++]->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_DOUBLE:
      assert(sig_bt[i + 1] == T_VOID, "expecting half");
      if (fp_args < Argument::n_float_register_parameters_j) {
        regs[i].set2(FP_ArgReg[fp_args++]->as_VMReg());
      } else {
        regs[i].set2(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    default:
      ShouldNotReachHere();
      break;
    }
  }

  return stk_args;
}

int SharedRuntime::c_calling_convention(const BasicType *sig_bt,
                                         VMRegPair *regs,
                                         VMRegPair *regs2,
                                         int total_args_passed) {
  assert(regs2 == NULL, "not needed on RISC-V");
  static const Register INT_ArgReg[Argument::n_int_register_parameters_c] = {
    c_rarg0, c_rarg1, c_rarg2, c_rarg3, c_rarg4, c_rarg5, c_rarg6, c_rarg7
  };
  static const FloatRegister FP_ArgReg[Argument::n_float_register_parameters_c] = {
    c_farg0, c_farg1, c_farg2, c_farg3, c_farg4, c_farg5, c_farg6, c_farg7
  };

  uint int_args = 0;
  uint fp_args = 0;
  uint stk_args = 0;

  for (int i = 0; i < total_args_passed; i++) {
    switch (sig_bt[i]) {
    case T_BOOLEAN:
    case T_CHAR:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
      if (int_args < Argument::n_int_register_parameters_c) {
        regs[i].set1(INT_ArgReg[int_args++]->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_VOID:
      assert(i != 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "expecting half");
      regs[i].set_bad();
      break;
    case T_LONG:
      assert(sig_bt[i + 1] == T_VOID, "expecting half");
      // fall through
    case T_OBJECT:
    case T_ARRAY:
    case T_ADDRESS:
    case T_METADATA:
      if (int_args < Argument::n_int_register_parameters_c) {
        regs[i].set2(INT_ArgReg[int_args++]->as_VMReg());
      } else {
        regs[i].set2(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_FLOAT:
      if (fp_args < Argument::n_float_register_parameters_c) {
        regs[i].set1(FP_ArgReg[fp_args++]->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_DOUBLE:
      assert(sig_bt[i + 1] == T_VOID, "expecting half");
      if (fp_args < Argument::n_float_register_parameters_c) {
        regs[i].set2(FP_ArgReg[fp_args++]->as_VMReg());
      } else {
        regs[i].set2(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    default:
      ShouldNotReachHere();
      break;
    }
  }

  return stk_args;
}

void SharedRuntime::save_native_result(MacroAssembler *masm, BasicType ret_type, int frame_slots) {
  switch (ret_type) {
  case T_FLOAT:
    masm->fsw(f10, Address(sp, 0));
    break;
  case T_DOUBLE:
    masm->fsd(f10, Address(sp, 0));
    break;
  case T_VOID:
    break;
  default:
    masm->sd(x10, Address(sp, 0));
    break;
  }
}

void SharedRuntime::restore_native_result(MacroAssembler *masm, BasicType ret_type, int frame_slots) {
  switch (ret_type) {
  case T_FLOAT:
    masm->flw(f10, Address(sp, 0));
    break;
  case T_DOUBLE:
    masm->fld(f10, Address(sp, 0));
    break;
  case T_VOID:
    break;
  default:
    masm->ld(x10, Address(sp, 0));
    break;
  }
}

// -------------------------------------------------------------
// Safepoint & Resolve Blobs
// -------------------------------------------------------------
SafepointBlob* SharedRuntime::generate_handler_blob(address call_ptr, int poll_type) {
  ResourceMark rm;
  OopMapSet *oop_maps = new OopMapSet();
  CodeBuffer buffer("handler_blob", 2048, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);

  address start = __ pc();
  int frame_size_in_words;
  bool cause_return = (poll_type == POLL_AT_RETURN);

  OopMap* map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);

  __ set_last_Java_frame(sp, noreg, NULL, t0);

  if (!cause_return) {
    __ ld(c_rarg0, Address(xthread, JavaThread::saved_exception_pc_offset()));
    __ sd(c_rarg0, Address(fp, -1 * wordSize));
  }

  __ mv(c_rarg0, xthread);
  __ call(call_ptr);

  oop_maps->add_gc_map(__ pc() - start, map);
  __ reset_last_Java_frame(false);

  Label noException;
  __ ld(t0, Address(xthread, Thread::pending_exception_offset()));
  __ beqz(t0, noException);

  RegisterSaver::restore_live_registers(masm);
  __ jmp(StubRoutines::forward_exception_entry());

  __ bind(noException);
  RegisterSaver::restore_live_registers(masm);
  __ ret();

  masm->flush();
  return SafepointBlob::create(&buffer, oop_maps, frame_size_in_words);
}

RuntimeStub* SharedRuntime::generate_resolve_blob(address destination, const char* name) {
  ResourceMark rm;
  CodeBuffer buffer(name, 1000, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);

  int frame_size_in_words;
  OopMapSet *oop_maps = new OopMapSet();
  int start = __ offset();

  OopMap* map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);

  int frame_complete = __ offset();

  Label retaddr;
  __ set_last_Java_frame(sp, noreg, retaddr, t0);
  __ mv(c_rarg0, xthread);
  __ call(destination);
  __ bind(retaddr);

  oop_maps->add_gc_map(__ offset() - start, map);
  __ reset_last_Java_frame(false);

  // Check for pending exceptions
  Label pending;
  __ ld(t0, Address(xthread, Thread::pending_exception_offset()));
  __ bnez(t0, pending);

  // Get the returned Method*
  __ get_vm_result_2(xmethod, xthread);
  __ sd(xmethod, Address(sp, RegisterSaver::reg_offset_in_bytes(xmethod)));

  // Destination address to jump to was returned in x10.
  // Overwrite the saved t0 slot so restore_live_registers restores it into t0.
  __ sd(x10, Address(sp, RegisterSaver::reg_offset_in_bytes(t0)));
  RegisterSaver::restore_live_registers(masm);

  // We are back to the original state on entry and ready to go.
  __ jr(t0);

  // Pending exception after the call
  __ bind(pending);

  RegisterSaver::restore_live_registers(masm);

  // Exception pending => remove activation and forward to exception handler
  __ sd(zr, Address(xthread, JavaThread::vm_result_offset()));
  __ ld(x10, Address(xthread, Thread::pending_exception_offset()));
  __ jmp(StubRoutines::forward_exception_entry());

  masm->flush();
  return RuntimeStub::new_runtime_stub(name, &buffer, frame_complete, frame_size_in_words, oop_maps, false);
}

// -------------------------------------------------------------
// Deoptimization & Uncommon Trap Blobs
// -------------------------------------------------------------
void SharedRuntime::generate_deopt_blob() {
  ResourceMark rm;
  CodeBuffer buffer("deopt_blob", 2048, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  int frame_size_in_words;
  OopMapSet *oop_maps = new OopMapSet();

  address start = __ pc();
  Label cont;

  // 1. Normal deopt
  OopMap* map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_deopt);
  __ j(cont);

  int reexecute_offset = __ pc() - start;

  // 2. Reexecute deopt
  (void) RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_reexecute);
  __ j(cont);

  int exception_offset = __ pc() - start;

  // 3. Exception deopt
  (void) RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_exception);

  __ bind(cont);

  __ set_last_Java_frame(sp, noreg, NULL, t0);
  __ mv(c_rarg0, xthread);
  __ mv(c_rarg1, xcpool);
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::fetch_unroll_info));

  __ reset_last_Java_frame(false);

  // UnrollBlock* is returned in x10
  __ mv(t2, x10);

  // Restore live registers before unpacking frames
  RegisterSaver::restore_live_registers(masm);

  // Call unpack_frames(JavaThread* thread, int exec_mode)
  __ set_last_Java_frame(sp, noreg, NULL, t0);
  __ mv(c_rarg0, xthread);
  __ mv(c_rarg1, xcpool);
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames));
  __ reset_last_Java_frame(false);

  // Target PC returned in x10
  __ jr(x10);

  masm->flush();
  _deopt_blob = DeoptimizationBlob::create(&buffer, oop_maps, 0, reexecute_offset, exception_offset, 0);
}

#ifdef COMPILER2
void SharedRuntime::generate_uncommon_trap_blob() {
  ResourceMark rm;
  CodeBuffer buffer("uncommon_trap_blob", 2048, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  int frame_size_in_words;
  OopMapSet *oop_maps = new OopMapSet();

  OopMap* map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);

  __ set_last_Java_frame(sp, noreg, NULL, t0);
  __ mv(c_rarg0, xthread);
  __ mv(c_rarg1, c_rarg1); // reason passed in c_rarg1
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::uncommon_trap));

  __ reset_last_Java_frame(false);
  RegisterSaver::restore_live_registers(masm);
  __ ret();

  masm->flush();
  _uncommon_trap_blob = UncommonTrapBlob::create(&buffer, oop_maps, frame_size_in_words);
}
#endif // COMPILER2

// -------------------------------------------------------------
// Method Adapters (i2c / c2i)
// -------------------------------------------------------------
static void gen_i2c_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs) {
  // Pass args from stack to registers
  __ mv(sp, esp);
  __ andi(sp, sp, -16);
}

static void gen_c2i_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs,
                            Label& skip_fixup) {
  // Push args to stack
  __ mv(xlocals, sp);
  __ ld(t1, Address(xmethod, Method::interpreter_entry_offset()));
  __ jr(t1);
}

AdapterHandlerEntry* SharedRuntime::generate_i2c2i_adapters(MacroAssembler *masm,
                                                            int total_args_passed,
                                                            int comp_args_on_stack,
                                                            const BasicType *sig_bt,
                                                            const VMRegPair *regs,
                                                            AdapterFingerPrint* fingerprint) {
  address i2c_entry = __ pc();
  gen_i2c_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs);

  address c2i_unverified_entry = __ pc();
  Label skip_fixup;

  address c2i_entry = __ pc();
  gen_c2i_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs, skip_fixup);

  return AdapterHandlerLibrary::new_entry(fingerprint, i2c_entry, c2i_entry, c2i_unverified_entry);
}

// -------------------------------------------------------------
// Native Wrapper for JNI Calls
// -------------------------------------------------------------
nmethod* SharedRuntime::generate_native_wrapper(MacroAssembler* masm,
                                                methodHandle method,
                                                int compile_id,
                                                BasicType* in_sig_bt,
                                                VMRegPair* in_regs,
                                                BasicType ret_type) {
  address native_func = method->native_function();
  assert(native_func != NULL, "must have native function");

  intptr_t start = (intptr_t)__ pc();
  int vep_offset = ((intptr_t)__ pc()) - start;

  // Prologue
  __ enter();

  // Call C native function
  __ call(native_func);

  // Epilogue
  __ leave();
  __ ret();

  __ flush();
  return nmethod::new_native_nmethod(method, compile_id, masm->code(), vep_offset, 0, 0, in_ByteSize(-1), in_ByteSize(-1), (OopMapSet*)NULL);
}

#ifdef HAVE_DTRACE_H
nmethod* SharedRuntime::generate_dtrace_nmethod(MacroAssembler *masm, methodHandle method) {
  return NULL;
}
#endif

int Deoptimization::last_frame_adjust(int callee_parameters, int callee_locals) {
  assert(callee_locals >= callee_parameters,
          "test and remove; got more parms than locals");
  if (callee_locals < callee_parameters)
    return 0;                   // No adjustment for negative locals
  int diff = (callee_locals - callee_parameters) * Interpreter::stackElementWords;
  // diff is counted in stack words
  return round_to(diff, 2);
}

