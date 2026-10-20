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
#include "asm/macroAssembler.inline.hpp"
#include "code/compiledIC.hpp"
#include "code/debugInfoRec.hpp"
#include "code/icBuffer.hpp"
#include "code/nmethod.hpp"
#include "code/scopeDesc.hpp"
#include "code/vtableStubs.hpp"
#include "compiler/oopMap.hpp"
#include "interpreter/interpreter.hpp"
#include "interp_masm_riscv.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/compiledICHolder.hpp"
#include "oops/klass.inline.hpp"
#include "oops/method.hpp"
#include "prims/jvmtiRedefineClassesTrace.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/signature.hpp"
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

const int StackAlignmentInSlots = StackAlignmentInBytes / VMRegImpl::stack_slot_size;

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

  static int freg_offset_in_bytes(FloatRegister f) {
    return (32 + f->encoding()) * wordSize;
  }
};

OopMap* RegisterSaver::save_live_registers(MacroAssembler* masm, int additional_frame_words, int* total_frame_words, bool save_vectors) {
  int frame_words = reg_save_words + additional_frame_words;
  frame_words = round_to(frame_words, 2); // 16-byte aligned
  *total_frame_words = frame_words;

  // OopMap frame size and VMRegImpl::stack2reg offsets are in 4-byte compiler stack slots (jint's),
  // NOT 8-byte words! (1 word = 2 slots).
  int frame_size_in_slots = frame_words * wordSize / BytesPerInt;
  int additional_frame_slots = additional_frame_words * wordSize / BytesPerInt;
  OopMap* map = new OopMap(frame_size_in_slots, 0);

  __ addi(sp, sp, -frame_words * wordSize);
  __ sd(ra, Address(sp, (frame_words - 1) * wordSize));
  __ sd(fp, Address(sp, (frame_words - 2) * wordSize));
  __ addi(fp, sp, frame_words * wordSize);

  // Save integer registers: x1..x31
  for (int i = 1; i < 32; i++) {
    Register r = as_Register(i);
    if (r != sp && r != fp && r != ra) {
      int sp_offset_in_slots = 2 * (i - 1);
      __ sd(r, Address(sp, (i - 1) * wordSize));
      if (r != xthread && r != xheapbase && r != t0) {
        map->set_callee_saved(VMRegImpl::stack2reg(sp_offset_in_slots + additional_frame_slots), r->as_VMReg());
      }
    }
  }

  // Save floating point registers: f0..f31
  int f_base = 32;
  for (int i = 0; i < 32; i++) {
    FloatRegister f = as_FloatRegister(i);
    int sp_offset_in_slots = 2 * (f_base + i);
    __ fsd(f, Address(sp, (f_base + i) * wordSize));
    map->set_callee_saved(VMRegImpl::stack2reg(sp_offset_in_slots + additional_frame_slots), f->as_VMReg());
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
      } else if (int_args < Argument::n_int_register_parameters_c) {
        regs[i].set1(INT_ArgReg[int_args++]->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stk_args));
        stk_args += 2;
      }
      break;
    case T_DOUBLE:
      assert(sig_bt[i + 1] == T_VOID, "expecting half");
      if (fp_args < Argument::n_float_register_parameters_c) {
        regs[i].set2(FP_ArgReg[fp_args++]->as_VMReg());
      } else if (int_args < Argument::n_int_register_parameters_c) {
        regs[i].set2(INT_ArgReg[int_args++]->as_VMReg());
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
  // We always ignore the frame_slots arg and just use the space just below saved fp/ra
  // (fp[-1] = ra, fp[-2] = saved_fp, fp[-3] = 2 slots reserved for moves/return value)
  switch (ret_type) {
  case T_FLOAT:
    __ fsw(f10, Address(fp, -3 * wordSize));
    break;
  case T_DOUBLE:
    __ fsd(f10, Address(fp, -3 * wordSize));
    break;
  case T_VOID:
    break;
  default:
    __ sd(x10, Address(fp, -3 * wordSize));
    break;
  }
}

void SharedRuntime::restore_native_result(MacroAssembler *masm, BasicType ret_type, int frame_slots) {
  // We always ignore the frame_slots arg and just use the space just below saved fp/ra
  switch (ret_type) {
  case T_FLOAT:
    __ flw(f10, Address(fp, -3 * wordSize));
    break;
  case T_DOUBLE:
    __ fld(f10, Address(fp, -3 * wordSize));
    break;
  case T_VOID:
    break;
  default:
    __ ld(x10, Address(fp, -3 * wordSize));
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

  Label retaddr;
  __ set_last_Java_frame(sp, noreg, retaddr, t0);

  if (!cause_return) {
    __ ld(c_rarg0, Address(xthread, JavaThread::saved_exception_pc_offset()));
    __ sd(c_rarg0, Address(fp, frame::return_addr_offset * wordSize));
  }

  __ mv(c_rarg0, xthread);
  __ call(call_ptr);
  __ bind(retaddr);

  oop_maps->add_gc_map(__ pc() - start, map);
  __ reset_last_Java_frame(false);

  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);

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
  int frame_size_in_words = -1;
  OopMap* map = NULL;
  OopMapSet *oop_maps = new OopMapSet();

  address start = __ pc();
  Label cont;

  // 1. Normal deopt
  map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_deopt);
  __ j(cont);

  int reexecute_offset = __ pc() - start;

  // 2. Reexecute deopt
  (void) RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_reexecute);
  __ j(cont);

  int exception_offset = __ pc() - start;

  // 3. Exception deopt (passed in x10 = exception_oop, x13 = exception_pc)
  __ sd(x13, Address(xthread, JavaThread::exception_pc_offset()));
  __ sd(x10, Address(xthread, JavaThread::exception_oop_offset()));

  int exception_in_tls_offset = __ pc() - start;

  // 4. Exception in TLS deopt
  map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);
  __ li(xcpool, Deoptimization::Unpack_exception);

  // Load throwing pc from JavaThread and patch it as the return address
  // of the current frame, then clear the field in JavaThread.
  __ ld(x13, Address(xthread, JavaThread::exception_pc_offset()));
  __ sd(x13, Address(fp, frame::return_addr_offset * wordSize));
  __ sd(zr, Address(xthread, JavaThread::exception_pc_offset()));

#ifdef ASSERT
  // Verify that there is really an exception oop in JavaThread
  __ ld(x10, Address(xthread, JavaThread::exception_oop_offset()));
  __ verify_oop(x10);

  // Verify that there is no pending exception
  {
    Label no_pending_exception;
    __ ld(t0, Address(xthread, Thread::pending_exception_offset()));
    __ beqz(t0, no_pending_exception);
    __ stop("must not have pending exception here");
    __ bind(no_pending_exception);
  }
#endif

  __ bind(cont);

  // Call fetch_unroll_info(JavaThread* thread)
  Label retaddr;
  __ set_last_Java_frame(sp, noreg, retaddr, t0);
  __ mv(c_rarg0, xthread);
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::fetch_unroll_info));
  __ bind(retaddr);

  oop_maps->add_gc_map(__ pc() - start, map);
  __ reset_last_Java_frame(false);

  // Load UnrollBlock* into x15
  __ mv(x15, x10);

  Label noException;
  __ li(t0, Deoptimization::Unpack_exception);
  __ bne(xcpool, t0, noException);
  __ ld(x10, Address(xthread, JavaThread::exception_oop_offset()));
  __ ld(x13, Address(xthread, JavaThread::exception_pc_offset()));
  __ sd(zr, Address(xthread, JavaThread::exception_oop_offset()));
  __ sd(zr, Address(xthread, JavaThread::exception_pc_offset()));

  __ verify_oop(x10);

  // Overwrite the result register with the exception oop
  __ sd(x10, Address(sp, RegisterSaver::reg_offset_in_bytes(x10)));

  __ bind(noException);

  // Restore result registers (f10 and x10); everything else is either dead
  // or captured in the vframeArray.
  __ fld(f10, Address(sp, RegisterSaver::freg_offset_in_bytes(f10)));
  __ ld(x10, Address(sp, RegisterSaver::reg_offset_in_bytes(x10)));

  // Pop all of the register save area off the stack
  __ add(sp, sp, frame_size_in_words * wordSize);

  // Pop deoptimized frame:
  // In our frame layout, enter() stores saved_fp at sp[size - 2*wordSize] (fp[-2])
  // and ra at sp[size - 1*wordSize] (fp[-1]).
  __ lwu(x12, Address(x15, Deoptimization::UnrollBlock::size_of_deoptimized_frame_offset_in_bytes()));
  __ sub(x12, x12, 2 * wordSize);
  __ add(sp, sp, x12);
  __ ld(fp, Address(sp, 0));
  __ ld(ra, Address(sp, wordSize));
  __ addi(sp, sp, 2 * wordSize);
  // RA should now be the return address to the caller (frame 3)

#ifdef ASSERT
  if (UseStackBanging) {
    __ lwu(x9, Address(x15, Deoptimization::UnrollBlock::total_frame_sizes_offset_in_bytes()));
    __ bang_stack_size(x9, x12);
  }
#endif

  // Load address of array of frame pcs into x12
  __ ld(x12, Address(x15, Deoptimization::UnrollBlock::frame_pcs_offset_in_bytes()));

  // Load address of array of frame sizes into x14
  __ ld(x14, Address(x15, Deoptimization::UnrollBlock::frame_sizes_offset_in_bytes()));

  // Load counter into x13
  __ lwu(x13, Address(x15, Deoptimization::UnrollBlock::number_of_frames_offset_in_bytes()));

  // Adjust the caller's stack to make up for the extra locals, recording original sp
  const Register sender_sp = x16;
  __ mv(sender_sp, sp);
  __ lwu(x9, Address(x15, Deoptimization::UnrollBlock::caller_adjustment_offset_in_bytes()));
  __ sub(sp, sp, x9);

  // Push interpreter frames in a loop
  Label loop;
  __ bind(loop);
  __ ld(x9, Address(x14, 0));          // Load frame size
  __ addi(x14, x14, wordSize);
  __ sub(x9, x9, 2 * wordSize);        // We'll push pc and fp by hand via enter()
  __ ld(ra, Address(x12, 0));          // Load pc
  __ addi(x12, x12, wordSize);
  __ enter();                          // Save old fp & ra, set new fp
  __ sub(sp, sp, x9);                  // Prolog
  __ sd(zr, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ sd(sender_sp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
  __ mv(sender_sp, sp);                // Pass sender_sp to next frame
  __ addi(x13, x13, -1);               // Decrement counter
  __ bnez(x13, loop);

  // Re-push self-frame
  __ ld(ra, Address(x12, 0));
  __ enter();

  // Allocate a full sized register save area (subtract 2 because enter() pushed 2 words)
  __ sub(sp, sp, (frame_size_in_words - 2) * wordSize);

  // Restore frame locals after moving the frame
  __ fsd(f10, Address(sp, RegisterSaver::freg_offset_in_bytes(f10)));
  __ sd(x10, Address(sp, RegisterSaver::reg_offset_in_bytes(x10)));

  // Call Deoptimization::unpack_frames(JavaThread* thread, int exec_mode)
  address the_pc = __ pc();
  __ set_last_Java_frame(sp, fp, the_pc, t0);

  __ mv(c_rarg0, xthread);
  __ mv(c_rarg1, xcpool); // second arg: exec_mode
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames));

  // Set an oopmap for the call site (in 4-byte compiler stack slots!)
  oop_maps->add_gc_map(the_pc - start, new OopMap(frame_size_in_words * 2, 0));

  // Clear fp AND pc
  __ reset_last_Java_frame(true);

  // Collect return values
  __ fld(f10, Address(sp, RegisterSaver::freg_offset_in_bytes(f10)));
  __ ld(x10, Address(sp, RegisterSaver::reg_offset_in_bytes(x10)));

  // Pop self-frame
  __ leave();

  // Jump to interpreter
  __ ret();

  masm->flush();
  _deopt_blob = DeoptimizationBlob::create(&buffer, oop_maps, 0, exception_offset, reexecute_offset, frame_size_in_words);
  _deopt_blob->set_unpack_with_exception_in_tls_offset(exception_in_tls_offset);
}

#ifdef COMPILER2
void SharedRuntime::generate_uncommon_trap_blob() {
  ResourceMark rm;
  CodeBuffer buffer("uncommon_trap_blob", 2048, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  int frame_size_in_words;
  OopMapSet *oop_maps = new OopMapSet();
  address start = __ pc();

  OopMap* map = RegisterSaver::save_live_registers(masm, 0, &frame_size_in_words);

  Label retaddr;
  __ set_last_Java_frame(sp, noreg, retaddr, t0);
  __ mv(c_rarg0, xthread);
  __ mv(c_rarg1, c_rarg1); // reason passed in c_rarg1
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::uncommon_trap));
  __ bind(retaddr);

  oop_maps->add_gc_map(__ pc() - start, map);
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
// Patch the caller's callsite with entry to compiled code if it exists.
static void patch_callers_callsite(MacroAssembler *masm) {
  Label L;
  __ ld(t0, Address(xmethod, in_bytes(Method::code_offset())));
  __ beqz(t0, L);

  __ enter();
  __ push_CPU_state();

  // VM needs caller's callsite and target method
  __ mv(c_rarg0, xmethod);
  __ mv(c_rarg1, ra);
  __ call(CAST_FROM_FN_PTR(address, SharedRuntime::fixup_callers_callsite));

  // Explicit fence.i required because fixup_callers_callsite may change the code stream
  __ fence_i();

  __ pop_CPU_state();
  __ leave();
  __ bind(L);
}

static void gen_c2i_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs,
                            Label& skip_fixup) {
  // Check if there is a compiled target and patch caller's callsite if so.
  patch_callers_callsite(masm);

  __ bind(skip_fixup);

  int words_pushed = 0;

  // Since all args are passed on the stack, total_args_passed *
  // Interpreter::stackElementSize is the space we need.
  int extraspace = total_args_passed * Interpreter::stackElementSize;

  // Record caller's unextended SP in x19_sender_sp (expected by our template interpreter!)
  __ mv(x19_sender_sp, sp);

  // Stack is aligned, keep it that way
  extraspace = round_to(extraspace, 2 * wordSize);

  if (extraspace) {
    __ sub(sp, sp, extraspace);
  }

  // Write the args into the outgoing interpreter space
  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      assert(i > 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "missing half");
      continue;
    }

    // Offset to start parameters
    int st_off   = (total_args_passed - i - 1) * Interpreter::stackElementSize;
    int next_off = st_off - Interpreter::stackElementSize;

    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }
    if (r_1->is_stack()) {
      // memory to memory use t0
      int ld_off = (r_1->reg2stack() * VMRegImpl::stack_slot_size
                    + extraspace
                    + words_pushed * wordSize);
      if (!r_2->is_valid()) {
        __ lwu(t0, Address(sp, ld_off), /*temp register*/esp);
        __ sd(t0, Address(sp, st_off), /*temp register*/esp);
      } else {
        __ ld(t0, Address(sp, ld_off), /*temp register*/esp);

        // Two VMRegs|OptoRegs can be T_OBJECT, T_ADDRESS, T_DOUBLE, T_LONG
        // T_DOUBLE and T_LONG use two slots in the interpreter
        if (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) {
          // ld_off == LSW, ld_off+wordSize == MSW
          // st_off == MSW, next_off == LSW
          __ sd(t0, Address(sp, next_off), /*temp register*/esp);
#ifdef ASSERT
          // Overwrite the unused slot with known junk
          __ li(t0, 0xdeadffffdeadaaaaul);
          __ sd(t0, Address(sp, st_off), /*temp register*/esp);
#endif /* ASSERT */
        } else {
          __ sd(t0, Address(sp, st_off), /*temp register*/esp);
        }
      }
    } else if (r_1->is_Register()) {
      Register r = r_1->as_Register();
      if (!r_2->is_valid()) {
        // must be only an int (or less) so move only 32bits to slot
        __ sd(r, Address(sp, st_off), /*temp register*/esp);
      } else {
        // Two VMRegs|OptoRegs can be T_OBJECT, T_ADDRESS, T_DOUBLE, T_LONG
        // T_DOUBLE and T_LONG use two slots in the interpreter
        if (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) {
          // long/double in gpr
#ifdef ASSERT
          // Overwrite the unused slot with known junk
          __ li(t0, 0xdeadffffdeadaaabul);
          __ sd(t0, Address(sp, st_off), /*temp register*/esp);
#endif /* ASSERT */
          __ sd(r, Address(sp, next_off), /*temp register*/esp);
        } else {
          __ sd(r, Address(sp, st_off), /*temp register*/esp);
        }
      }
    } else {
      assert(r_1->is_FloatRegister(), "");
      if (!r_2->is_valid()) {
        // only a float use just part of the slot
        __ fsw(r_1->as_FloatRegister(), Address(sp, st_off), /*temp register*/esp);
      } else {
#ifdef ASSERT
        // Overwrite the unused slot with known junk
        __ li(t0, 0xdeadffffdeadaaacul);
        __ sd(t0, Address(sp, st_off), /*temp register*/esp);
#endif /* ASSERT */
        __ fsd(r_1->as_FloatRegister(), Address(sp, next_off), /*temp register*/esp);
      }
    }
  }

  __ mv(esp, sp); // Interp expects args on caller's expression stack

  __ ld(t0, Address(xmethod, in_bytes(Method::interpreter_entry_offset())));
  __ jr(t0);
}

static void gen_i2c_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs) {
  // Cut-out for having no stack args.
  int comp_words_on_stack = round_to(comp_args_on_stack * VMRegImpl::stack_slot_size, wordSize) >> LogBytesPerWord;
  if (comp_args_on_stack != 0) {
    __ sub(t0, sp, comp_words_on_stack * wordSize);
    __ andi(sp, t0, -16);
  }

  // Will jump to the compiled code just as if compiled code was doing it.
  // Pre-load the register-jump target early, to schedule it better.
  __ ld(t1, Address(xmethod, in_bytes(Method::from_compiled_offset())));

  // Now generate the shuffle code.
  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      assert(i > 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "missing half");
      continue;
    }

    // Pick up 0, 1 or 2 words from SP+offset.
    assert(!regs[i].second()->is_valid() || regs[i].first()->next() == regs[i].second(),
           "scrambled load targets?");
    // Load in argument order going down.
    int ld_off = (total_args_passed - i - 1) * Interpreter::stackElementSize;
    // Point to interpreter value (vs. tag)
    int next_off = ld_off - Interpreter::stackElementSize;

    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }
    if (r_1->is_stack()) {
      // Convert stack slot to an SP offset
      int st_off = regs[i].first()->reg2stack() * VMRegImpl::stack_slot_size;
      if (!r_2->is_valid()) {
        __ lw(t0, Address(esp, ld_off), /*temp register*/t2);
        __ sd(t0, Address(sp, st_off), /*temp register*/t2);
      } else {
        // We are using two optoregs. This can be either T_OBJECT,
        // T_ADDRESS, T_LONG, or T_DOUBLE. The interpreter allocates
        // two slots but only uses one for the T_LONG or T_DOUBLE case.
        const int offset = (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) ?
                           next_off : ld_off;
        __ ld(t0, Address(esp, offset), /*temp register*/t2);
        __ sd(t0, Address(sp, st_off), /*temp register*/t2);
      }
    } else if (r_1->is_Register()) {
      Register r = r_1->as_Register();
      if (r_2->is_valid()) {
        const int offset = (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) ?
                           next_off : ld_off;
        __ ld(r, Address(esp, offset), /*temp register*/t0);
      } else {
        __ lw(r, Address(esp, ld_off), /*temp register*/t0);
      }
    } else {
      if (!r_2->is_valid()) {
        __ flw(r_1->as_FloatRegister(), Address(esp, ld_off), /*temp register*/t0);
      } else {
        __ fld(r_1->as_FloatRegister(), Address(esp, next_off), /*temp register*/t0);
      }
    }
  }

  // 6243940: Stash the desired callee in JavaThread::callee_target in case
  // we end up in handle_wrong_method if the callee is deoptimized as we race thru here.
  __ sd(xmethod, Address(xthread, JavaThread::callee_target_offset()));

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
  Label ok;

  const Register holder = t1;
  const Register receiver = j_rarg0;
  const Register tmp = t2; // A call-clobbered register not used for arg passing

  {
    __ block_comment("c2i_unverified_entry {");
    __ load_klass(t0, receiver);
    __ ld(tmp, Address(holder, CompiledICHolder::holder_klass_offset()));
    __ ld(xmethod, Address(holder, CompiledICHolder::holder_metadata_offset()));
    __ beq(t0, tmp, ok);
    __ far_jump(RuntimeAddress(SharedRuntime::get_ic_miss_stub()));

    __ bind(ok);
    // Method might have been compiled since the call site was patched to
    // interpreted; if that is the case treat it as a miss so we can get
    // the call site corrected.
    __ ld(t0, Address(xmethod, in_bytes(Method::code_offset())));
    __ beqz(t0, skip_fixup);
    __ far_jump(RuntimeAddress(SharedRuntime::get_ic_miss_stub()));
    __ block_comment("} c2i_unverified_entry");
  }

  address c2i_entry = __ pc();

  gen_c2i_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs, skip_fixup);

  __ flush();
  return AdapterHandlerLibrary::new_entry(fingerprint, i2c_entry, c2i_entry, c2i_unverified_entry);
}

// -------------------------------------------------------------
// Native Wrapper for JNI Calls
// -------------------------------------------------------------
static int reg2offset_in(VMReg r) {
  // In our frame layout, enter() sets fp to caller's sp (fp[-1] = ra, fp[-2] = saved_fp).
  // Incoming stack arguments start at caller's sp + 0, i.e., fp + 0.
  return r->reg2stack() * VMRegImpl::stack_slot_size;
}

static int reg2offset_out(VMReg r) {
  return (r->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
}

static void move32_64(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  if (src.first()->is_stack()) {
    if (dst.first()->is_stack()) {
      // stack to stack
      __ ld(t0, Address(fp, reg2offset_in(src.first())));
      __ sd(t0, Address(sp, reg2offset_out(dst.first())));
    } else {
      // stack to reg
      __ lw(dst.first()->as_Register(), Address(fp, reg2offset_in(src.first())));
    }
  } else if (dst.first()->is_stack()) {
    // reg to stack
    __ sd(src.first()->as_Register(), Address(sp, reg2offset_out(dst.first())));
  } else {
    if (dst.first() != src.first()) {
      // 32-bit sign extend
      __ addiw(dst.first()->as_Register(), src.first()->as_Register(), 0);
    }
  }
}

// An oop arg. Must pass a handle not the oop itself
static void object_move(MacroAssembler* masm,
                        OopMap* map,
                        int oop_handle_offset,
                        int framesize_in_slots,
                        VMRegPair src,
                        VMRegPair dst,
                        bool is_receiver,
                        int* receiver_offset) {
  // Must pass a handle. First figure out the location we use as a handle.
  Register rHandle = dst.first()->is_stack() ? t1 : dst.first()->as_Register();

  if (src.first()->is_stack()) {
    // Oop is already on the stack as an argument
    int offset_in_older_frame = src.first()->reg2stack() + SharedRuntime::out_preserve_stack_slots();
    map->set_oop(VMRegImpl::stack2reg(offset_in_older_frame + framesize_in_slots));
    if (is_receiver) {
      *receiver_offset = (offset_in_older_frame + framesize_in_slots) * VMRegImpl::stack_slot_size;
    }

    __ ld(t0, Address(fp, reg2offset_in(src.first())));
    __ la(rHandle, Address(fp, reg2offset_in(src.first())));
    // Conditionally move a NULL
    Label notZero1;
    __ bnez(t0, notZero1);
    __ mv(rHandle, zr);
    __ bind(notZero1);
  } else {
    // Oop is in a register; we must store it to the space we reserve
    // on the stack for oop_handles and pass a handle if oop is non-NULL
    const Register rOop = src.first()->as_Register();
    int oop_slot = -1;
    if (rOop == j_rarg0) {
      oop_slot = 0;
    } else if (rOop == j_rarg1) {
      oop_slot = 1;
    } else if (rOop == j_rarg2) {
      oop_slot = 2;
    } else if (rOop == j_rarg3) {
      oop_slot = 3;
    } else if (rOop == j_rarg4) {
      oop_slot = 4;
    } else if (rOop == j_rarg5) {
      oop_slot = 5;
    } else if (rOop == j_rarg6) {
      oop_slot = 6;
    } else {
      assert(rOop == j_rarg7, "wrong register");
      oop_slot = 7;
    }

    oop_slot = oop_slot * VMRegImpl::slots_per_word + oop_handle_offset;
    int offset = oop_slot * VMRegImpl::stack_slot_size;

    map->set_oop(VMRegImpl::stack2reg(oop_slot));
    // Store oop in handle area, may be NULL
    __ sd(rOop, Address(sp, offset));
    if (is_receiver) {
      *receiver_offset = offset;
    }

    // rOop may be the same as rHandle
    if (rOop == rHandle) {
      Label isZero;
      __ beqz(rOop, isZero);
      __ la(rHandle, Address(sp, offset));
      __ bind(isZero);
    } else {
      Label notZero2;
      __ la(rHandle, Address(sp, offset));
      __ bnez(rOop, notZero2);
      __ mv(rHandle, zr);
      __ bind(notZero2);
    }
  }

  // If arg is on the stack then place it otherwise it is already in correct reg.
  if (dst.first()->is_stack()) {
    __ sd(rHandle, Address(sp, reg2offset_out(dst.first())));
  }
}

static void float_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  assert(src.first()->is_stack() && dst.first()->is_stack() ||
         src.first()->is_reg() && dst.first()->is_reg() ||
         src.first()->is_stack() && dst.first()->is_reg(), "Unexpected error");
  if (src.first()->is_stack()) {
    if (dst.first()->is_stack()) {
      __ lwu(t0, Address(fp, reg2offset_in(src.first())));
      __ sw(t0, Address(sp, reg2offset_out(dst.first())));
    } else if (dst.first()->is_Register()) {
      __ lwu(dst.first()->as_Register(), Address(fp, reg2offset_in(src.first())));
    } else {
      ShouldNotReachHere();
    }
  } else if (src.first() != dst.first()) {
    if (src.is_single_phys_reg() && dst.is_single_phys_reg()) {
      __ fmv_s(dst.first()->as_FloatRegister(), src.first()->as_FloatRegister());
    } else {
      ShouldNotReachHere();
    }
  }
}

static void long_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  if (src.first()->is_stack()) {
    if (dst.first()->is_stack()) {
      // stack to stack
      __ ld(t0, Address(fp, reg2offset_in(src.first())));
      __ sd(t0, Address(sp, reg2offset_out(dst.first())));
    } else {
      // stack to reg
      __ ld(dst.first()->as_Register(), Address(fp, reg2offset_in(src.first())));
    }
  } else if (dst.first()->is_stack()) {
    // reg to stack
    __ sd(src.first()->as_Register(), Address(sp, reg2offset_out(dst.first())));
  } else {
    if (dst.first() != src.first()) {
      __ mv(dst.first()->as_Register(), src.first()->as_Register());
    }
  }
}

static void double_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  assert(src.first()->is_stack() && dst.first()->is_stack() ||
         src.first()->is_reg() && dst.first()->is_reg() ||
         src.first()->is_stack() && dst.first()->is_reg(), "Unexpected error");
  if (src.first()->is_stack()) {
    if (dst.first()->is_stack()) {
      __ ld(t0, Address(fp, reg2offset_in(src.first())));
      __ sd(t0, Address(sp, reg2offset_out(dst.first())));
    } else if (dst.first()->is_Register()) {
      __ ld(dst.first()->as_Register(), Address(fp, reg2offset_in(src.first())));
    } else {
      ShouldNotReachHere();
    }
  } else if (src.first() != dst.first()) {
    if (src.is_single_phys_reg() && dst.is_single_phys_reg()) {
      __ fmv_d(dst.first()->as_FloatRegister(), src.first()->as_FloatRegister());
    } else {
      ShouldNotReachHere();
    }
  }
}

static void save_args(MacroAssembler *masm, int arg_count, int first_arg, VMRegPair *args) {
  RegSet x;
  for (int i = first_arg; i < arg_count; i++) {
    if (args[i].first()->is_Register()) {
      x = x + args[i].first()->as_Register();
    } else if (args[i].first()->is_FloatRegister()) {
      __ addi(sp, sp, -2 * wordSize);
      __ fsd(args[i].first()->as_FloatRegister(), Address(sp, 0));
    }
  }
  __ push_reg(x.bits(), sp);
}

static void restore_args(MacroAssembler *masm, int arg_count, int first_arg, VMRegPair *args) {
  RegSet x;
  for (int i = first_arg; i < arg_count; i++) {
    if (args[i].first()->is_Register()) {
      x = x + args[i].first()->as_Register();
    }
  }
  __ pop_reg(x.bits(), sp);
  for (int i = arg_count - 1; i >= first_arg; i--) {
    if (args[i].first()->is_FloatRegister()) {
      __ fld(args[i].first()->as_FloatRegister(), Address(sp, 0));
      __ addi(sp, sp, 2 * wordSize);
    }
  }
}

static void rt_call(MacroAssembler* masm, address dest) {
  CodeBlob *cb = CodeCache::find_blob(dest);
  if (cb) {
    __ far_call(RuntimeAddress(dest));
  } else {
    int32_t offset = 0;
    __ la_patchable(t0, RuntimeAddress(dest), offset);
    __ jalr(x1, t0, offset);
  }
}

static void verify_oop_args(MacroAssembler* masm,
                            methodHandle method,
                            const BasicType* sig_bt,
                            const VMRegPair* regs) {
  const Register temp_reg = x9; // not part of any compiled calling seq
  if (VerifyOops) {
    for (int i = 0; i < method->size_of_parameters(); i++) {
      if (sig_bt[i] == T_OBJECT || sig_bt[i] == T_ARRAY) {
        VMReg r = regs[i].first();
        assert(r->is_valid(), "bad oop arg");
        if (r->is_stack()) {
          __ ld(temp_reg, Address(sp, r->reg2stack() * VMRegImpl::stack_slot_size));
          __ verify_oop(temp_reg);
        } else {
          __ verify_oop(r->as_Register());
        }
      }
    }
  }
}

static void gen_special_dispatch(MacroAssembler* masm,
                                 methodHandle method,
                                 const BasicType* sig_bt,
                                 const VMRegPair* regs) {
  verify_oop_args(masm, method, sig_bt, regs);
  vmIntrinsics::ID iid = method->intrinsic_id();

  // Write the args into the outgoing interpreter space
  bool     has_receiver   = false;
  Register receiver_reg   = noreg;
  int      member_arg_pos = -1;
  Register member_reg     = noreg;
  int      ref_kind       = MethodHandles::signature_polymorphic_intrinsic_ref_kind(iid);
  if (ref_kind != 0) {
    member_arg_pos = method->size_of_parameters() - 1;  // trailing MemberName argument
    member_reg = x9;  // known to be free at this point
    has_receiver = MethodHandles::ref_kind_has_receiver(ref_kind);
  } else if (iid == vmIntrinsics::_invokeBasic) {
    has_receiver = true;
  } else {
    fatal(err_msg_res("unexpected intrinsic id %d", iid));
  }

  if (member_reg != noreg) {
    // Load the member_arg into register, if necessary.
    SharedRuntime::check_member_name_argument_is_last_argument(method, sig_bt, regs);
    VMReg r = regs[member_arg_pos].first();
    if (r->is_stack()) {
      __ ld(member_reg, Address(sp, r->reg2stack() * VMRegImpl::stack_slot_size));
    } else {
      // no data motion is needed
      member_reg = r->as_Register();
    }
  }

  if (has_receiver) {
    // Make sure the receiver is loaded into a register.
    assert(method->size_of_parameters() > 0, "oob");
    assert(sig_bt[0] == T_OBJECT, "receiver argument must be an object");
    VMReg r = regs[0].first();
    assert(r->is_valid(), "bad receiver arg");
    if (r->is_stack()) {
      fatal("receiver always in a register");
      receiver_reg = x12; // known to be free at this point
      __ ld(receiver_reg, Address(sp, r->reg2stack() * VMRegImpl::stack_slot_size));
    } else {
      // no data motion is needed
      receiver_reg = r->as_Register();
    }
  }

  // Figure out which address we are really jumping to:
  MethodHandles::generate_method_handle_dispatch(masm, iid,
                                                 receiver_reg, member_reg, /*for_compiler_entry:*/ true);
}

nmethod* SharedRuntime::generate_native_wrapper(MacroAssembler* masm,
                                                methodHandle method,
                                                int compile_id,
                                                BasicType* in_sig_bt,
                                                VMRegPair* in_regs,
                                                BasicType ret_type) {
  if (method->is_method_handle_intrinsic()) {
    vmIntrinsics::ID iid = method->intrinsic_id();
    intptr_t start = (intptr_t)__ pc();
    int vep_offset = ((intptr_t)__ pc()) - start;

    // First instruction must be a nop as it may need to be patched on deoptimisation
    __ nop();
    gen_special_dispatch(masm,
                         method,
                         in_sig_bt,
                         in_regs);
    int frame_complete = ((intptr_t)__ pc()) - start;  // not complete, period
    __ flush();
    int stack_slots = SharedRuntime::out_preserve_stack_slots();  // no out slots at all, actually
    return nmethod::new_native_nmethod(method,
                                       compile_id,
                                       masm->code(),
                                       vep_offset,
                                       frame_complete,
                                       stack_slots / VMRegImpl::slots_per_word,
                                       in_ByteSize(-1),
                                       in_ByteSize(-1),
                                       (OopMapSet*)NULL);
  }

  address native_func = method->native_function();
  assert(native_func != NULL, "must have function");

  // An OopMap for lock (and class if static)
  OopMapSet *oop_maps = new OopMapSet();
  intptr_t start = (intptr_t)__ pc();

  const int total_in_args = method->size_of_parameters();
  int total_c_args = total_in_args + (method->is_static() ? 2 : 1);

  BasicType* out_sig_bt = NEW_RESOURCE_ARRAY(BasicType, total_c_args);
  VMRegPair* out_regs   = NEW_RESOURCE_ARRAY(VMRegPair, total_c_args);

  int argc = 0;
  out_sig_bt[argc++] = T_ADDRESS;
  if (method->is_static()) {
    out_sig_bt[argc++] = T_OBJECT;
  }

  for (int i = 0; i < total_in_args; i++) {
    out_sig_bt[argc++] = in_sig_bt[i];
  }

  // Now figure out where the args must be stored and how much stack space they require.
  int out_arg_slots = c_calling_convention(out_sig_bt, out_regs, NULL, total_c_args);

  // First count the abi requirement plus all of the outgoing args
  int stack_slots = SharedRuntime::out_preserve_stack_slots() + out_arg_slots;

  // Now the space for the inbound oop handle area
  int total_save_slots = 8 * VMRegImpl::slots_per_word; // 8 arguments passed in registers

  int oop_handle_offset = stack_slots;
  stack_slots += total_save_slots;

  // Now any space we need for handlizing a klass if static method
  int klass_slot_offset = 0;
  int klass_offset = -1;
  int lock_slot_offset = 0;
  bool is_static = false;

  if (method->is_static()) {
    klass_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
    klass_offset = klass_slot_offset * VMRegImpl::stack_slot_size;
    is_static = true;
  }

  // Plus a lock if needed
  if (method->is_synchronized()) {
    lock_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
  }

  // Now a place (+2) to save return values or temp during shuffling
  // + 4 for return address (which we own) and saved fp
  stack_slots += 6;

  // Round to make stack properly aligned
  stack_slots = round_to(stack_slots, StackAlignmentInSlots);
  int stack_size = stack_slots * VMRegImpl::stack_slot_size;

  // First thing make an ic check to see if we should even be here
  const Register ic_reg = t1;
  const Register receiver = j_rarg0;

  Label hit;
  Label exception_pending;

  assert_different_registers(ic_reg, receiver, t0);
  __ verify_oop(receiver);
  __ cmp_klass(receiver, ic_reg, t0, hit);

  __ far_jump(RuntimeAddress(SharedRuntime::get_ic_miss_stub()));

  // Verified entry point must be aligned
  __ align(8);

  __ bind(hit);

  int vep_offset = ((intptr_t)__ pc()) - start;

  // First instruction at VEP must be a 4-byte nop for patching (make_not_entrant)
  __ nop();

  // Generate stack overflow check
  if (UseStackBanging) {
    __ bang_stack_with_offset(StackShadowPages * os::vm_page_size());
  }

  // Generate a new frame for the wrapper.
  __ enter();
  // -2 because return address is already present and so is saved fp
  __ sub(sp, sp, stack_size - 2 * wordSize);

  // Frame is now completed as far as size and linkage.
  int frame_complete = ((intptr_t)__ pc()) - start;

  // We use x18 as the oop handle for the receiver/klass (callee-saved)
  const Register oop_handle_reg = x18;

  // -----------------
  // The Grand Shuffle
  int receiver_offset = -1;

  OopMap* map = new OopMap(stack_slots * 2, 0 /* arg_slots*/);

  int float_args = 0;
  int int_args = 0;

#ifdef ASSERT
  bool reg_destroyed[RegisterImpl::number_of_registers];
  bool freg_destroyed[FloatRegisterImpl::number_of_registers];
  for (int r = 0; r < RegisterImpl::number_of_registers; r++) {
    reg_destroyed[r] = false;
  }
  for (int f = 0; f < FloatRegisterImpl::number_of_registers; f++) {
    freg_destroyed[f] = false;
  }
#endif /* ASSERT */

  // For JNI natives the incoming and outgoing registers are offset upwards.
  GrowableArray<int> arg_order(2 * total_in_args);
  for (int i = total_in_args - 1, c_arg = total_c_args - 1; i >= 0; i--, c_arg--) {
    arg_order.push(i);
    arg_order.push(c_arg);
  }

  for (int ai = 0; ai < arg_order.length(); ai += 2) {
    int i = arg_order.at(ai);
    int c_arg = arg_order.at(ai + 1);
    __ block_comment(err_msg("mv %d -> %d", i, c_arg));
    assert(c_arg != -1 && i != -1, "wrong order");
#ifdef ASSERT
    if (in_regs[i].first()->is_Register()) {
      assert(!reg_destroyed[in_regs[i].first()->as_Register()->encoding()], "destroyed reg!");
    } else if (in_regs[i].first()->is_FloatRegister()) {
      assert(!freg_destroyed[in_regs[i].first()->as_FloatRegister()->encoding()], "destroyed reg!");
    }
    if (out_regs[c_arg].first()->is_Register()) {
      reg_destroyed[out_regs[c_arg].first()->as_Register()->encoding()] = true;
    } else if (out_regs[c_arg].first()->is_FloatRegister()) {
      freg_destroyed[out_regs[c_arg].first()->as_FloatRegister()->encoding()] = true;
    }
#endif /* ASSERT */
    switch (in_sig_bt[i]) {
      case T_ARRAY:
      case T_OBJECT:
        object_move(masm, map, oop_handle_offset, stack_slots, in_regs[i], out_regs[c_arg],
                    ((i == 0) && (!is_static)),
                    &receiver_offset);
        int_args++;
        break;
      case T_VOID:
        break;
      case T_FLOAT:
        float_move(masm, in_regs[i], out_regs[c_arg]);
        float_args++;
        break;
      case T_DOUBLE:
        assert(i + 1 < total_in_args &&
               in_sig_bt[i + 1] == T_VOID &&
               out_sig_bt[c_arg + 1] == T_VOID, "bad arg list");
        double_move(masm, in_regs[i], out_regs[c_arg]);
        float_args++;
        break;
      case T_LONG:
        long_move(masm, in_regs[i], out_regs[c_arg]);
        int_args++;
        break;
      case T_ADDRESS:
        assert(false, "found T_ADDRESS in java args");
        break;
      default:
        move32_64(masm, in_regs[i], out_regs[c_arg]);
        int_args++;
    }
  }

  // Point c_arg at the first arg that is already loaded in case we
  // need to spill before we call out
  int c_arg = total_c_args - total_in_args;

  // Pre-load a static method's oop into c_rarg1.
  if (method->is_static()) {
    __ movoop(c_rarg1,
              JNIHandles::make_local(method->method_holder()->java_mirror()),
              /*immediate*/true);

    // Handlize the static class mirror
    __ sd(c_rarg1, Address(sp, klass_offset));
    map->set_oop(VMRegImpl::stack2reg(klass_slot_offset));

    // Get the handle
    __ la(c_rarg1, Address(sp, klass_offset));
    c_arg--;
  }

  // Change state to native
  Label native_return;
  __ set_last_Java_frame(sp, noreg, native_return, t0);

  Label dtrace_method_entry, dtrace_method_entry_done;
  {
    int32_t offset = 0;
    __ la_patchable(t0, ExternalAddress((address)&DTraceMethodProbes), offset);
    __ lbu(t0, Address(t0, offset));
    __ bnez(t0, dtrace_method_entry);
    __ bind(dtrace_method_entry_done);
  }

  // RedefineClasses() tracing support for obsolete method entry
  if (RC_TRACE_IN_RANGE(0x00001000, 0x00002000)) {
    save_args(masm, total_c_args, c_arg, out_regs);
    __ mov_metadata(c_rarg1, method());
    __ call_VM_leaf(
      CAST_FROM_FN_PTR(address, SharedRuntime::rc_trace_method_entry),
      xthread, c_rarg1);
    restore_args(masm, total_c_args, c_arg, out_regs);
  }

  // Lock a synchronized method
  const Register swap_reg = x10;
  const Register obj_reg  = x9;  // Will contain the oop
  const Register lock_reg = x30; // Address of compiler lock object (BasicLock)
  const Register old_hdr  = x30; // value of old header at unlock time
  const Register tmp      = ra;

  Label slow_path_lock;
  Label lock_done;

  if (method->is_synchronized()) {
    const int mark_word_offset = BasicLock::displaced_header_offset_in_bytes();

    // Get the handle (the 2nd argument)
    __ mv(oop_handle_reg, c_rarg1);

    // Get address of the box
    __ la(lock_reg, Address(sp, lock_slot_offset * VMRegImpl::stack_slot_size));

    // Load the oop from the handle
    __ ld(obj_reg, Address(oop_handle_reg, 0));

    if (UseBiasedLocking) {
      __ biased_locking_enter(lock_reg, obj_reg, swap_reg, tmp, false, lock_done, &slow_path_lock);
    }

    // Load (object->mark() | 1) into swap_reg %x10
    __ ld(t0, Address(obj_reg, oopDesc::mark_offset_in_bytes()));
    __ ori(swap_reg, t0, 1);

    // Save (object->mark() | 1) into BasicLock's displaced header
    __ sd(swap_reg, Address(lock_reg, mark_word_offset));

    // CAS: src -> dest iff dest == x10 else x10 <- dest
    __ cmpxchg_obj_header(x10, lock_reg, obj_reg, t0, lock_done, /*fallthrough*/NULL);

    // Test if the oopMark is an obvious stack pointer, i.e.,
    // 1) (mark & 7) == 0, and 2) sp <= mark < sp + os::pagesize()
    __ sub(swap_reg, swap_reg, sp);
    __ li(t0, 7 - os::vm_page_size());
    __ andr(swap_reg, swap_reg, t0);

    // Save the test result, for recursive case, the result is zero
    __ sd(swap_reg, Address(lock_reg, mark_word_offset));
    __ bnez(swap_reg, slow_path_lock);

    // Slow path will re-enter here
    __ bind(lock_done);
  }

  // Get JNIEnv* which is first argument to native
  __ la(c_rarg0, Address(xthread, in_bytes(JavaThread::jni_environment_offset())));

  // Set thread in native
  __ la(t1, Address(xthread, JavaThread::thread_state_offset()));
  __ mv(t0, _thread_in_native);
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ sw(t0, Address(t1, 0));

  rt_call(masm, native_func);

  __ bind(native_return);

  intptr_t return_pc = (intptr_t) __ pc();
  oop_maps->add_gc_map(return_pc - start, map);

  // Unpack native results.
  if (ret_type != T_OBJECT && ret_type != T_ARRAY && ret_type != T_VOID) {
    __ cast_primitive_type(ret_type, x10);
  }

  // Switch thread to "native transition" state before reading the synchronization state.
  __ mv(t0, _thread_in_native_trans);
  __ sw(t0, Address(xthread, JavaThread::thread_state_offset()));

  // Force this write out before the read below
  if (os::is_MP()) {
    if (UseMembar) {
      __ membar(MacroAssembler::AnyAny);
    } else {
      __ serialize_memory(xthread, t1);
    }
  }

  // Check for safepoint operation in progress and/or pending suspend requests
  Label safepoint_in_progress, safepoint_in_progress_done;
  {
    assert(SafepointSynchronize::_not_synchronized == 0, "fix this code");
    __ movptr(t0, (intptr_t)SafepointSynchronize::address_of_state());
    __ lwu(t0, Address(t0, 0));
    __ bnez(t0, safepoint_in_progress);
    __ lwu(t0, Address(xthread, JavaThread::suspend_flags_offset()));
    __ bnez(t0, safepoint_in_progress);
    __ bind(safepoint_in_progress_done);
  }

  // Change thread state to _thread_in_Java
  Label after_transition;
  __ la(t1, Address(xthread, JavaThread::thread_state_offset()));
  __ mv(t0, _thread_in_Java);
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ sw(t0, Address(t1, 0));
  __ bind(after_transition);

  Label reguard;
  Label reguard_done;
  __ lbu(t0, Address(xthread, JavaThread::stack_guard_state_offset()));
  __ mv(t1, JavaThread::stack_guard_yellow_disabled);
  __ beq(t0, t1, reguard);
  __ bind(reguard_done);

  // Unlock
  Label unlock_done;
  Label slow_path_unlock;
  if (method->is_synchronized()) {
    // Get locked oop from the handle we passed to jni
    __ ld(obj_reg, Address(oop_handle_reg, 0));

    Label done;

    if (UseBiasedLocking) {
      __ biased_locking_exit(obj_reg, old_hdr, done);
    }

    // Simple recursive lock?
    __ ld(t0, Address(sp, lock_slot_offset * VMRegImpl::stack_slot_size));
    __ beqz(t0, done);

    // Must save x10 if it is live now because cmpxchg must use it
    if (ret_type != T_FLOAT && ret_type != T_DOUBLE && ret_type != T_VOID) {
      save_native_result(masm, ret_type, stack_slots);
    }

    // Get address of the stack lock
    __ la(x10, Address(sp, lock_slot_offset * VMRegImpl::stack_slot_size));
    // Get old displaced header
    __ ld(old_hdr, Address(x10, 0));

    // Atomic swap old header if oop still contains the stack lock
    Label succeed;
    __ cmpxchg_obj_header(x10, old_hdr, obj_reg, t0, succeed, &slow_path_unlock);
    __ bind(succeed);

    // slow path re-enters here
    __ bind(unlock_done);
    if (ret_type != T_FLOAT && ret_type != T_DOUBLE && ret_type != T_VOID) {
      restore_native_result(masm, ret_type, stack_slots);
    }

    __ bind(done);
  }

  Label dtrace_method_exit, dtrace_method_exit_done;
  {
    int32_t offset = 0;
    __ la_patchable(t0, ExternalAddress((address)&DTraceMethodProbes), offset);
    __ lbu(t0, Address(t0, offset));
    __ bnez(t0, dtrace_method_exit);
    __ bind(dtrace_method_exit_done);
  }

  __ reset_last_Java_frame(false);

  // Unbox oop result, e.g. JNIHandles::resolve result.
  if (ret_type == T_OBJECT || ret_type == T_ARRAY) {
    Label done, not_weak;
    __ beqz(x10, done);           // Use NULL as-is.
    STATIC_ASSERT(JNIHandles::weak_tag_mask == 1u);
    __ test_bit(t0, x10, 0);      // Test for jweak tag.
    __ beqz(t0, not_weak);
    // Resolve jweak.
    __ ld(x10, Address(x10, -JNIHandles::weak_tag_value));
    __ verify_oop(x10);
#if INCLUDE_ALL_GCS
    if (UseG1GC) {
      __ g1_write_barrier_pre(noreg /* obj */,
                              x10 /* pre_val */,
                              xthread /* thread */,
                              t1 /* tmp */,
                              true /* tosca_live */,
                              true /* expand_call */);
    }
#endif // INCLUDE_ALL_GCS
    __ j(done);
    __ bind(not_weak);
    // Resolve (untagged) jobject.
    __ ld(x10, Address(x10, 0));
    __ verify_oop(x10);
    __ bind(done);
  }

  // Reset handle block
  __ ld(x12, Address(xthread, JavaThread::active_handles_offset()));
  __ sd(zr, Address(x12, JNIHandleBlock::top_offset_in_bytes()));

  __ leave();

  // Any exception pending?
  __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
  __ bnez(t0, exception_pending);

  // We're done
  __ ret();

  // Unexpected paths are out of line and go here

  // Forward the exception
  __ bind(exception_pending);
  __ far_jump(RuntimeAddress(StubRoutines::forward_exception_entry()));

  // Slow path locking & unlocking
  if (method->is_synchronized()) {
    __ block_comment("Slow path lock {");
    __ bind(slow_path_lock);

    // Protect the args we've loaded
    save_args(masm, total_c_args, c_arg, out_regs);

    __ mv(c_rarg0, obj_reg);
    __ mv(c_rarg1, lock_reg);
    __ mv(c_rarg2, xthread);

    // Not a leaf but we have last_Java_frame setup as we want
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_locking_C), 3);
    restore_args(masm, total_c_args, c_arg, out_regs);

#ifdef ASSERT
    {
      Label L;
      __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
      __ beqz(t0, L);
      __ stop("no pending exception allowed on exit from monitorenter");
      __ bind(L);
    }
#endif
    __ j(lock_done);
    __ block_comment("} Slow path lock");

    __ block_comment("Slow path unlock {");
    __ bind(slow_path_unlock);

    if (ret_type == T_FLOAT || ret_type == T_DOUBLE) {
      save_native_result(masm, ret_type, stack_slots);
    }

    __ la(c_rarg1, Address(sp, lock_slot_offset * VMRegImpl::stack_slot_size));
    __ mv(c_rarg0, obj_reg);

    // Save pending exception around call to VM (which contains an EXCEPTION_MARK)
    // NOTE that obj_reg == x9 currently
    __ ld(x9, Address(xthread, in_bytes(Thread::pending_exception_offset())));
    __ sd(zr, Address(xthread, in_bytes(Thread::pending_exception_offset())));

    rt_call(masm, CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_unlocking_C));

#ifdef ASSERT
    {
      Label L;
      __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
      __ beqz(t0, L);
      __ stop("no pending exception allowed on exit complete_monitor_unlocking_C");
      __ bind(L);
    }
#endif /* ASSERT */

    __ sd(x9, Address(xthread, in_bytes(Thread::pending_exception_offset())));

    if (ret_type == T_FLOAT || ret_type == T_DOUBLE) {
      restore_native_result(masm, ret_type, stack_slots);
    }
    __ j(unlock_done);
    __ block_comment("} Slow path unlock");
  }

  // SLOW PATH Reguard the stack if needed
  __ bind(reguard);
  save_native_result(masm, ret_type, stack_slots);
  rt_call(masm, CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages));
  restore_native_result(masm, ret_type, stack_slots);
  __ j(reguard_done);

  // SLOW PATH safepoint
  {
    __ block_comment("safepoint {");
    __ bind(safepoint_in_progress);

    // Don't use call_VM as it will see a possible pending exception and forward it
    // and never return here preventing us from clearing _last_native_pc down below.
    save_native_result(masm, ret_type, stack_slots);
    __ mv(c_rarg0, xthread);
#ifndef PRODUCT
    assert(frame::arg_reg_save_area_bytes == 0, "not expecting frame reg save area");
#endif
    int32_t offset = 0;
    __ la_patchable(t0, RuntimeAddress(CAST_FROM_FN_PTR(address, JavaThread::check_special_condition_for_native_trans)), offset);
    __ jalr(x1, t0, offset);

    // Restore any method result value
    restore_native_result(masm, ret_type, stack_slots);

    __ j(safepoint_in_progress_done);
    __ block_comment("} safepoint");
  }

  // SLOW PATH dtrace support
  {
    __ block_comment("dtrace entry {");
    __ bind(dtrace_method_entry);

    save_args(masm, total_c_args, c_arg, out_regs);
    __ mov_metadata(c_rarg1, method());
    __ call_VM_leaf(
      CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_entry),
      xthread, c_rarg1);
    restore_args(masm, total_c_args, c_arg, out_regs);
    __ j(dtrace_method_entry_done);
    __ block_comment("} dtrace entry");
  }

  {
    __ block_comment("dtrace exit {");
    __ bind(dtrace_method_exit);
    save_native_result(masm, ret_type, stack_slots);
    __ mov_metadata(c_rarg1, method());
    __ call_VM_leaf(
         CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_exit),
         xthread, c_rarg1);
    restore_native_result(masm, ret_type, stack_slots);
    __ j(dtrace_method_exit_done);
    __ block_comment("} dtrace exit");
  }

  __ flush();

  nmethod *nm = nmethod::new_native_nmethod(method,
                                            compile_id,
                                            masm->code(),
                                            vep_offset,
                                            frame_complete,
                                            stack_slots / VMRegImpl::slots_per_word,
                                            (is_static ? in_ByteSize(klass_offset) : in_ByteSize(receiver_offset)),
                                            in_ByteSize(lock_slot_offset * VMRegImpl::stack_slot_size),
                                            oop_maps);
  return nm;
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

