/*
 * Copyright (c) 1997, 2026, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2015, Red Hat Inc. All rights reserved.
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
#include "asm/macroAssembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/barrierSet.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "memory/universe.hpp"
#include "nativeInst_riscv.hpp"
#include "oops/instanceOop.hpp"
#include "oops/method.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.inline.hpp"
#include "utilities/top.hpp"
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif

// Declaration and definition of StubGenerator (no .hpp file).
// For a more detailed description of the stub routine structure
// see the comment in stubRoutines.hpp

#undef __
#define __ _masm->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

class StubGenerator: public StubCodeGenerator {
 private:

  // Call stub stack layout word offsets from fp
  enum call_stub_layout {
    // Top of entry frame is at fp.
    // fp[ 0] = caller sp (sender_sp_offset = 0)
    // fp[-1] = return address ra (return_addr_offset = -1)
    // fp[-2] = saved fp (link_offset = -2)

    // C arguments:
    thread_off         =  -3, // c_rarg7 (a7)
    parameter_size_off =  -4, // c_rarg6 (a6)
    parameters_off     =  -5, // c_rarg5 (a5)
    entry_point_off    =  -6, // c_rarg4 (a4)
    method_off         =  -7, // c_rarg3 (a3)
    result_type_off    =  -8, // c_rarg2 (a2)
    result_off         =  -9, // c_rarg1 (a1)
    call_wrapper_off   = -10, // c_rarg0 (a0) -- entry_frame_call_wrapper_offset

    // Callee-saved integer registers s1-s11:
    s11_off            = -11, // x27 (s11 / xheapbase)
    s10_off            = -12, // x26 (s10 / xcpool)
    s9_off             = -13, // x25 (s9 / xmonitors)
    s8_off             = -14, // x24 (s8 / xlocals)
    s7_off             = -15, // x23 (s7 / xthread)
    s6_off             = -16, // x22 (s6 / xbcp)
    s5_off             = -17, // x21 (s5 / xdispatch)
    s4_off             = -18, // x20 (s4 / esp)
    s3_off             = -19, // x19 (s3)
    s2_off             = -20, // x18 (s2)
    s1_off             = -21, // x9  (s1)

    // Callee-saved float registers fs0-fs11:
    fs11_off           = -22, // f27 (fs11)
    fs10_off           = -23, // f26 (fs10)
    fs9_off            = -24, // f25 (fs9)
    fs8_off            = -25, // f24 (fs8)
    fs7_off            = -26, // f23 (fs7)
    fs6_off            = -27, // f22 (fs6)
    fs5_off            = -28, // f21 (fs5)
    fs4_off            = -29, // f20 (fs4)
    fs3_off            = -30, // f19 (fs3)
    fs2_off            = -31, // f18 (fs2)
    fs1_off            = -32, // f9  (fs1)
    fs0_off            = -33, // f8  (fs0)

    sp_after_call_off  = -34
  };

  // -------------------------------------------------------------
  // Call Stub: Bridge C++ to Java
  // -------------------------------------------------------------
  address generate_call_stub(address& return_address) {
    assert((int)frame::entry_frame_after_call_words == -(int)sp_after_call_off + 1 &&
           (int)frame::entry_frame_call_wrapper_offset == (int)call_wrapper_off,
           "adjust this code");

    StubCodeMark mark(this, "StubRoutines", "call_stub");
    address start = __ pc();

    // Set up frame and move sp to end of save area
    __ enter();
    __ addi(sp, fp, sp_after_call_off * wordSize);

    // Save C argument registers to stack
    __ sd(c_rarg7, Address(fp, thread_off * wordSize));
    __ sd(c_rarg6, Address(fp, parameter_size_off * wordSize));
    __ sd(c_rarg5, Address(fp, parameters_off * wordSize));
    __ sd(c_rarg4, Address(fp, entry_point_off * wordSize));
    __ sd(c_rarg3, Address(fp, method_off * wordSize));
    __ sd(c_rarg2, Address(fp, result_type_off * wordSize));
    __ sd(c_rarg1, Address(fp, result_off * wordSize));
    __ sd(c_rarg0, Address(fp, call_wrapper_off * wordSize));

    // Save callee-saved integer registers
    __ sd(x27, Address(fp, s11_off * wordSize));
    __ sd(x26, Address(fp, s10_off * wordSize));
    __ sd(x25, Address(fp, s9_off * wordSize));
    __ sd(x24, Address(fp, s8_off * wordSize));
    __ sd(x23, Address(fp, s7_off * wordSize));
    __ sd(x22, Address(fp, s6_off * wordSize));
    __ sd(x21, Address(fp, s5_off * wordSize));
    __ sd(x20, Address(fp, s4_off * wordSize));
    __ sd(x19, Address(fp, s3_off * wordSize));
    __ sd(x18, Address(fp, s2_off * wordSize));
    __ sd(x9,  Address(fp, s1_off * wordSize));

    // Save callee-saved float registers
    __ fsd(f27, Address(fp, fs11_off * wordSize));
    __ fsd(f26, Address(fp, fs10_off * wordSize));
    __ fsd(f25, Address(fp, fs9_off * wordSize));
    __ fsd(f24, Address(fp, fs8_off * wordSize));
    __ fsd(f23, Address(fp, fs7_off * wordSize));
    __ fsd(f22, Address(fp, fs6_off * wordSize));
    __ fsd(f21, Address(fp, fs5_off * wordSize));
    __ fsd(f20, Address(fp, fs4_off * wordSize));
    __ fsd(f19, Address(fp, fs3_off * wordSize));
    __ fsd(f18, Address(fp, fs2_off * wordSize));
    __ fsd(f9,  Address(fp, fs1_off * wordSize));
    __ fsd(f8,  Address(fp, fs0_off * wordSize));

    // Install JavaThread in dedicated register
    __ mv(xthread, c_rarg7);
    // Install Method in dedicated register
    __ mv(xmethod, c_rarg3);

    // Set up compressed oop base
    __ reinit_heapbase();

#ifdef ASSERT
    // Check for pending exceptions before entering Java
    {
      Label L;
      __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
      __ beqz(t0, L);
      __ stop("StubRoutines::call_stub: entered with pending exception");
      __ bind(L);
    }
#endif

    // Pass parameters if any
    __ mv(esp, sp);
    __ slli(t0, c_rarg6, LogBytesPerWord);
    __ sub(t0, sp, t0); // Move SP out of the way
    __ andi(sp, t0, -16);

    Label parameters_done;
    __ beqz(c_rarg6, parameters_done);

    Label loop;
    __ bind(loop);
    __ ld(t0, Address(c_rarg5, 0));
    __ addi(c_rarg5, c_rarg5, wordSize);
    __ subi(c_rarg6, c_rarg6, 1);
    __ push_reg(t0);
    __ bgtz(c_rarg6, loop);

    __ bind(parameters_done);

    // Call Java entry point:
    //   xmethod: Method*
    //   x19_sender_sp: sender sp
    __ mv(x19_sender_sp, sp);
    __ jalr(ra, c_rarg4, 0);

    // Exception handling code uses return_address to identify entry frames
    return_address = __ pc();

    // Store result depending on result type
    __ ld(a1, Address(fp, result_off * wordSize));
    __ ld(a2, Address(fp, result_type_off * wordSize));

    Label is_long, is_float, is_double, exit;
    __ li(t0, T_OBJECT); __ beq(a2, t0, is_long);
    __ li(t0, T_LONG);   __ beq(a2, t0, is_long);
    __ li(t0, T_FLOAT);  __ beq(a2, t0, is_float);
    __ li(t0, T_DOUBLE); __ beq(a2, t0, is_double);

    // Default: T_INT (and smaller types)
    __ sw(a0, Address(a1, 0));
    __ j(exit);

    __ bind(is_long);
    __ sd(a0, Address(a1, 0));
    __ j(exit);

    __ bind(is_float);
    __ fsw(fa0, Address(a1, 0));
    __ j(exit);

    __ bind(is_double);
    __ fsd(fa0, Address(a1, 0));
    __ j(exit);

    __ bind(exit);

    // Restore sp to end of save area
    __ addi(sp, fp, sp_after_call_off * wordSize);

#ifdef ASSERT
    // Verify thread register has not been corrupted
    {
      Label L;
      __ ld(t0, Address(fp, thread_off * wordSize));
      __ beq(xthread, t0, L);
      __ stop("StubRoutines::call_stub: threads must correspond");
      __ bind(L);
    }
#endif

    // Restore callee-saved float registers
    __ fld(f8,  Address(fp, fs0_off * wordSize));
    __ fld(f9,  Address(fp, fs1_off * wordSize));
    __ fld(f18, Address(fp, fs2_off * wordSize));
    __ fld(f19, Address(fp, fs3_off * wordSize));
    __ fld(f20, Address(fp, fs4_off * wordSize));
    __ fld(f21, Address(fp, fs5_off * wordSize));
    __ fld(f22, Address(fp, fs6_off * wordSize));
    __ fld(f23, Address(fp, fs7_off * wordSize));
    __ fld(f24, Address(fp, fs8_off * wordSize));
    __ fld(f25, Address(fp, fs9_off * wordSize));
    __ fld(f26, Address(fp, fs10_off * wordSize));
    __ fld(f27, Address(fp, fs11_off * wordSize));

    // Restore callee-saved integer registers
    __ ld(x9,  Address(fp, s1_off * wordSize));
    __ ld(x18, Address(fp, s2_off * wordSize));
    __ ld(x19, Address(fp, s3_off * wordSize));
    __ ld(x20, Address(fp, s4_off * wordSize));
    __ ld(x21, Address(fp, s5_off * wordSize));
    __ ld(x22, Address(fp, s6_off * wordSize));
    __ ld(x23, Address(fp, s7_off * wordSize));
    __ ld(x24, Address(fp, s8_off * wordSize));
    __ ld(x25, Address(fp, s9_off * wordSize));
    __ ld(x26, Address(fp, s10_off * wordSize));
    __ ld(x27, Address(fp, s11_off * wordSize));

    // Leave frame and return to C++ caller
    __ leave();
    __ ret();

    return start;
  }

  // -------------------------------------------------------------
  // Catch Exception Stub
  // -------------------------------------------------------------
  // a0: exception oop
  address generate_catch_exception() {
    StubCodeMark mark(this, "StubRoutines", "catch_exception");
    address start = __ pc();

#ifdef ASSERT
    {
      Label L;
      __ ld(t0, Address(fp, thread_off * wordSize));
      __ beq(xthread, t0, L);
      __ stop("StubRoutines::catch_exception: threads must correspond");
      __ bind(L);
    }
#endif

    // Set pending exception in JavaThread
    __ sd(a0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
    __ movptr(t0, (intptr_t)__FILE__);
    __ sd(t0, Address(xthread, in_bytes(Thread::exception_file_offset())));
    __ li(t0, __LINE__);
    __ sw(t0, Address(xthread, in_bytes(Thread::exception_line_offset())));

    // Complete return to VM
    assert(StubRoutines::_call_stub_return_address != NULL,
           "_call_stub_return_address must have been generated before");
    __ jmp(StubRoutines::_call_stub_return_address);

    return start;
  }

  // -------------------------------------------------------------
  // Forward Exception Stub
  // -------------------------------------------------------------
  // Entry: ra contains the return PC into Java (throwing PC)
  address generate_forward_exception() {
    StubCodeMark mark(this, "StubRoutines", "forward_exception");
    address start = __ pc();

#ifdef ASSERT
    {
      Label L;
      __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
      __ bnez(t0, L);
      __ stop("StubRoutines::forward_exception: no pending exception");
      __ bind(L);
    }
#endif

    // Save throwing PC across VM call in callee-saved x9
    __ mv(x9, ra);

    // Call runtime to find handler address
    __ call_VM_leaf(CAST_FROM_FN_PTR(address,
                         SharedRuntime::exception_handler_for_return_address),
                    xthread, ra);

    // Handler returned in a0; move to t1
    __ mv(t1, a0);
    // Restore throwing PC
    __ mv(ra, x9);
    __ mv(a3, x9);

    // Load exception and clear pending exception
    __ ld(a0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
    __ sd(zr, Address(xthread, in_bytes(Thread::pending_exception_offset())));

    // Jump to exception handler:
    //   a0: exception oop
    //   a3: throwing pc
    //   t1: handler pc
    __ jalr(zr, t1, 0);

    return start;
  }

  // -------------------------------------------------------------
  // Throw Exception Helper
  // -------------------------------------------------------------
  address generate_throw_exception(const char* name,
                                   address runtime_entry,
                                   Register arg1 = noreg,
                                   Register arg2 = noreg) {
    enum layout {
      fp_off = 0,
      ra_off = 1,
      framesize = 2
    };

    int insts_size = 512;
    int locs_size  = 64;

    CodeBuffer code(name, insts_size, locs_size);
    OopMapSet* oop_maps  = new OopMapSet();
    MacroAssembler* masm = new MacroAssembler(&code);

    address start = __ pc();

    __ enter();
    int frame_complete = __ pc() - start;

    address the_pc = __ pc();
    __ set_last_Java_frame(sp, fp, (address)NULL, t0);

    // Set arguments
    if (arg1 != noreg) {
      assert(arg2 != c_rarg1, "clobbered");
      __ mv(c_rarg1, arg1);
    }
    if (arg2 != noreg) {
      __ mv(c_rarg2, arg2);
    }
    __ mv(c_rarg0, xthread);
    __ call(runtime_entry);

    // Generate oop map
    OopMap* map = new OopMap(framesize, 0);
    oop_maps->add_gc_map(the_pc - start, map);

    __ reset_last_Java_frame(true);
    __ leave();

#ifdef ASSERT
    Label L;
    __ ld(t0, Address(xthread, in_bytes(Thread::pending_exception_offset())));
    __ bnez(t0, L);
    __ should_not_reach_here();
    __ bind(L);
#endif

    __ jmp(StubRoutines::forward_exception_entry());

    RuntimeStub* stub =
      RuntimeStub::new_runtime_stub(name,
                                    &code,
                                    frame_complete,
                                    framesize,
                                    oop_maps, false);
    return stub->entry_point();
  }

  // -------------------------------------------------------------
  // Verify OOP Stub
  // -------------------------------------------------------------
  address generate_verify_oop() {
    StubCodeMark mark(this, "StubRoutines", "verify_oop");
    address start = __ pc();

    Label exit, error;

    // Save registers on C stack (sp) with 16-byte alignment
    __ addi(sp, sp, -16);
    __ sd(a1, Address(sp, 8));
    __ sd(a2, Address(sp, 0));

    // Object is in a0. If obj == NULL, it is OK.
    __ beqz(a0, exit);

    // Check if oop is in valid heap memory range
    __ movptr(a1, (intptr_t)Universe::verify_oop_mask());
    __ andr(a2, a0, a1);
    __ movptr(a1, (intptr_t)Universe::verify_oop_bits());
    __ bne(a2, a1, error);

    // Load klass from mark / klass word
    __ ld(a1, Address(a0, oopDesc::klass_offset_in_bytes()));
    __ beqz(a1, error);

    __ bind(exit);
    __ ld(a2, Address(sp, 0));
    __ ld(a1, Address(sp, 8));
    __ addi(sp, sp, 16);
    __ ret();

    __ bind(error);
    __ stop("broken oop");
    return start;
  }

  // -------------------------------------------------------------
  // SafeFetch Stubs
  // -------------------------------------------------------------
  void generate_safefetch(const char* name, int size, address* entry,
                          address* fault_pc, address* continuation_pc) {
    StubCodeMark mark(this, "StubRoutines", name);
    *entry = __ pc();
    *fault_pc = __ pc();
    switch (size) {
      case 4:
        __ lw(c_rarg1, Address(c_rarg0, 0));
        break;
      case 8:
        __ ld(c_rarg1, Address(c_rarg0, 0));
        break;
      default:
        ShouldNotReachHere();
    }
    *continuation_pc = __ pc();
    __ mv(a0, c_rarg1);
    __ ret();
  }

  // -------------------------------------------------------------
  // Zero Longs Stub
  // -------------------------------------------------------------
  // Inputs: c_rarg0 = to (HeapWord*), c_rarg1 = count (in words)
  address generate_zero_longs() {
    StubCodeMark mark(this, "StubRoutines", "zero_longs");
    address start = __ pc();

    Label done, loop;
    __ beqz(c_rarg1, done);

    __ bind(loop);
    __ sd(zr, Address(c_rarg0, 0));
    __ addi(c_rarg0, c_rarg0, wordSize);
    __ addi(c_rarg1, c_rarg1, -1);
    __ bgtz(c_rarg1, loop);

    __ bind(done);
    __ ret();
    return start;
  }

  // -------------------------------------------------------------
  // Array Write Pre/Post Barriers for OOP Array Copy
  // [JDK8u RISC-V Port Backport Note]
  // Combines JDK 8u AArch64 BarrierSet dispatch (G1 SATB + CardTable + CMS StoreStore)
  // with JDK 19 GA RISC-V register-set preservation and card-marking loop.
  // -------------------------------------------------------------
  void gen_write_ref_array_pre_barrier(Register addr, Register count, bool dest_uninitialized, RegSet saved_regs) {
    BarrierSet* bs = Universe::heap()->barrier_set();
    switch (bs->kind()) {
#if INCLUDE_ALL_GCS
      case BarrierSet::G1SATBCT:
      case BarrierSet::G1SATBCTLogging:
        if (!dest_uninitialized) {
          Label done;
          Address in_progress(xthread, in_bytes(JavaThread::satb_mark_queue_offset() +
                                               PtrQueue::byte_offset_of_active()));
          if (in_bytes(PtrQueue::byte_width_of_active()) == 4) {
            __ lwu(t0, in_progress);
          } else {
            assert(in_bytes(PtrQueue::byte_width_of_active()) == 1, "Assumption");
            __ lbu(t0, in_progress);
          }
          __ beqz(t0, done);

          __ push_reg(saved_regs, sp);
          if (count == c_rarg0) {
            if (addr == c_rarg1) {
              __ mv(t0, c_rarg0);
              __ mv(c_rarg0, c_rarg1);
              __ mv(c_rarg1, t0);
            } else {
              __ mv(c_rarg1, count);
              __ mv(c_rarg0, addr);
            }
          } else {
            __ mv(c_rarg0, addr);
            __ mv(c_rarg1, count);
          }
          __ call_VM_leaf(CAST_FROM_FN_PTR(address, BarrierSet::static_write_ref_array_pre), 2);
          __ pop_reg(saved_regs, sp);
          __ bind(done);
        }
        break;
#endif // INCLUDE_ALL_GCS
      case BarrierSet::CardTableModRef:
      case BarrierSet::CardTableExtension:
      case BarrierSet::ModRef:
        break;
      default:
        ShouldNotReachHere();
    }
  }

  void gen_write_ref_array_post_barrier(Register start, Register count, Register scratch, RegSet saved_regs) {
    assert_different_registers(start, count, scratch, t1);
    Label L_done;

    __ beqz(count, L_done);

    BarrierSet* bs = Universe::heap()->barrier_set();
    switch (bs->kind()) {
#if INCLUDE_ALL_GCS
      case BarrierSet::G1SATBCT:
      case BarrierSet::G1SATBCTLogging:
        {
          __ push_reg(saved_regs, sp);
          if (count == c_rarg0) {
            if (start == c_rarg1) {
              __ mv(t0, c_rarg0);
              __ mv(c_rarg0, c_rarg1);
              __ mv(c_rarg1, t0);
            } else {
              __ mv(c_rarg1, count);
              __ mv(c_rarg0, start);
            }
          } else {
            __ mv(c_rarg0, start);
            __ mv(c_rarg1, count);
          }
          __ call_VM_leaf(CAST_FROM_FN_PTR(address, BarrierSet::static_write_ref_array_post), 2);
          __ pop_reg(saved_regs, sp);
        }
        break;
#endif // INCLUDE_ALL_GCS
      case BarrierSet::CardTableModRef:
      case BarrierSet::CardTableExtension:
        {
          Label L_loop;
          const Register end = t1;

          // end = start + (count << LogBytesPerHeapOop) - BytesPerHeapOop
          __ slli(end, count, LogBytesPerHeapOop);
          __ add(end, start, end);
          __ subi(end, end, BytesPerHeapOop);

          __ srli(start, start, CardTableModRefBS::card_shift);
          __ srli(end, end, CardTableModRefBS::card_shift);
          __ sub(count, end, start); // number of cards - 1

          __ load_byte_map_base(scratch);
          __ add(start, start, scratch);
          if (UseConcMarkSweepGC) {
            __ membar(MacroAssembler::StoreStore);
          }
          __ bind(L_loop);
          __ add(scratch, start, count);
          __ sb(zr, Address(scratch, 0));
          __ subi(count, count, 1);
          __ bgez(count, L_loop);
        }
        break;
      case BarrierSet::ModRef:
        break;
      default:
        ShouldNotReachHere();
    }
    __ bind(L_done);
  }

  // -------------------------------------------------------------
  // Disjoint Array Copy
  // -------------------------------------------------------------
  address generate_disjoint_copy(size_t elem_size, bool is_oop, address* entry, const char* name) {
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();
    if (entry != NULL) *entry = start;

    Label done;
    __ beqz(c_rarg2, done);

    Register s   = c_rarg0;
    Register d   = c_rarg1;
    Register cnt = c_rarg2;
    Register tmp = t0;

    if (is_oop) {
      gen_write_ref_array_pre_barrier(d, cnt, false, RegSet::of(s, d, cnt, ra));
      __ push_reg(RegSet::of(d, cnt, ra), sp);
    }

    Label loop;
    __ bind(loop);
    switch (elem_size) {
      case 1:
        __ lb(tmp, Address(s, 0));
        __ sb(tmp, Address(d, 0));
        break;
      case 2:
        __ lh(tmp, Address(s, 0));
        __ sh(tmp, Address(d, 0));
        break;
      case 4:
        __ lw(tmp, Address(s, 0));
        __ sw(tmp, Address(d, 0));
        break;
      case 8:
        __ ld(tmp, Address(s, 0));
        __ sd(tmp, Address(d, 0));
        break;
    }
    __ addi(s, s, elem_size);
    __ addi(d, d, elem_size);
    __ addi(cnt, cnt, -1);
    __ bgtz(cnt, loop);

    if (is_oop) {
      __ pop_reg(RegSet::of(d, cnt, ra), sp);
      gen_write_ref_array_post_barrier(d, cnt, t0, RegSet::of(ra));
    }

    __ bind(done);
    __ mv(a0, zr);
    __ ret();
    return start;
  }

  // -------------------------------------------------------------
  // Conjoint Array Copy
  // -------------------------------------------------------------
  address generate_conjoint_copy(size_t elem_size, bool is_oop, address nooverlap_target,
                                address* entry, const char* name) {
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();
    if (entry != NULL) *entry = start;

    Label done, copy_done;
    __ beqz(c_rarg2, done);

    Register s   = c_rarg0;
    Register d   = c_rarg1;
    Register cnt = c_rarg2;
    Register tmp = t0;

    if (is_oop) {
      gen_write_ref_array_pre_barrier(d, cnt, false, RegSet::of(s, d, cnt, ra));
      __ push_reg(RegSet::of(d, cnt, ra), sp);
    }

    Label copy_backwards;
    __ bltu(s, d, copy_backwards);

    // Forward copy loop
    Label loop_fwd;
    __ bind(loop_fwd);
    switch (elem_size) {
      case 1:
        __ lb(tmp, Address(s, 0));
        __ sb(tmp, Address(d, 0));
        break;
      case 2:
        __ lh(tmp, Address(s, 0));
        __ sh(tmp, Address(d, 0));
        break;
      case 4:
        __ lw(tmp, Address(s, 0));
        __ sw(tmp, Address(d, 0));
        break;
      case 8:
        __ ld(tmp, Address(s, 0));
        __ sd(tmp, Address(d, 0));
        break;
    }
    __ addi(s, s, elem_size);
    __ addi(d, d, elem_size);
    __ addi(cnt, cnt, -1);
    __ bgtz(cnt, loop_fwd);
    __ j(copy_done);

    // Backward copy loop
    __ bind(copy_backwards);
    Register s_end = t1;
    Register d_end = t2;
    switch (elem_size) {
      case 1: __ mv(tmp, cnt); break;
      case 2: __ slli(tmp, cnt, 1); break;
      case 4: __ slli(tmp, cnt, 2); break;
      case 8: __ slli(tmp, cnt, 3); break;
    }
    __ add(s_end, s, tmp);
    __ add(d_end, d, tmp);

    Label loop_bwd;
    __ bind(loop_bwd);
    __ addi(s_end, s_end, -elem_size);
    __ addi(d_end, d_end, -elem_size);
    switch (elem_size) {
      case 1:
        __ lb(tmp, Address(s_end, 0));
        __ sb(tmp, Address(d_end, 0));
        break;
      case 2:
        __ lh(tmp, Address(s_end, 0));
        __ sh(tmp, Address(d_end, 0));
        break;
      case 4:
        __ lw(tmp, Address(s_end, 0));
        __ sw(tmp, Address(d_end, 0));
        break;
      case 8:
        __ ld(tmp, Address(s_end, 0));
        __ sd(tmp, Address(d_end, 0));
        break;
    }
    __ addi(cnt, cnt, -1);
    __ bgtz(cnt, loop_bwd);

    __ bind(copy_done);
    if (is_oop) {
      __ pop_reg(RegSet::of(d, cnt, ra), sp);
      gen_write_ref_array_post_barrier(d, cnt, t0, RegSet::of(ra));
    }

    __ bind(done);
    __ mv(a0, zr);
    __ ret();
    return start;
  }

  // -------------------------------------------------------------
  // Generate All Arraycopy Stubs
  // -------------------------------------------------------------
  void generate_arraycopy_stubs() {
    address entry;
    address entry_jbyte_arraycopy;
    address entry_jshort_arraycopy;
    address entry_jint_arraycopy;
    address entry_oop_arraycopy;
    address entry_jlong_arraycopy;

    StubRoutines::riscv::_zero_longs = generate_zero_longs();

    // jbyte
    StubRoutines::_jbyte_disjoint_arraycopy         = generate_disjoint_copy(sizeof(jbyte), false, &entry, "jbyte_disjoint_arraycopy");
    StubRoutines::_jbyte_arraycopy                  = generate_conjoint_copy(sizeof(jbyte), false, entry, &entry_jbyte_arraycopy, "jbyte_arraycopy");
    StubRoutines::_arrayof_jbyte_disjoint_arraycopy = generate_disjoint_copy(sizeof(jbyte), false, &entry, "arrayof_jbyte_disjoint_arraycopy");
    StubRoutines::_arrayof_jbyte_arraycopy          = generate_conjoint_copy(sizeof(jbyte), false, entry, NULL, "arrayof_jbyte_arraycopy");

    // jshort
    StubRoutines::_jshort_disjoint_arraycopy         = generate_disjoint_copy(sizeof(jshort), false, &entry, "jshort_disjoint_arraycopy");
    StubRoutines::_jshort_arraycopy                  = generate_conjoint_copy(sizeof(jshort), false, entry, &entry_jshort_arraycopy, "jshort_arraycopy");
    StubRoutines::_arrayof_jshort_disjoint_arraycopy = generate_disjoint_copy(sizeof(jshort), false, &entry, "arrayof_jshort_disjoint_arraycopy");
    StubRoutines::_arrayof_jshort_arraycopy          = generate_conjoint_copy(sizeof(jshort), false, entry, NULL, "arrayof_jshort_arraycopy");

    // jint
    StubRoutines::_jint_disjoint_arraycopy         = generate_disjoint_copy(sizeof(jint), false, &entry, "jint_disjoint_arraycopy");
    StubRoutines::_jint_arraycopy                  = generate_conjoint_copy(sizeof(jint), false, entry, &entry_jint_arraycopy, "jint_arraycopy");
    StubRoutines::_arrayof_jint_disjoint_arraycopy = generate_disjoint_copy(sizeof(jint), false, &entry, "arrayof_jint_disjoint_arraycopy");
    StubRoutines::_arrayof_jint_arraycopy          = generate_conjoint_copy(sizeof(jint), false, entry, NULL, "arrayof_jint_arraycopy");

    // jlong
    StubRoutines::_jlong_disjoint_arraycopy         = generate_disjoint_copy(sizeof(jlong), false, &entry, "jlong_disjoint_arraycopy");
    StubRoutines::_jlong_arraycopy                  = generate_conjoint_copy(sizeof(jlong), false, entry, &entry_jlong_arraycopy, "jlong_arraycopy");
    StubRoutines::_arrayof_jlong_disjoint_arraycopy = generate_disjoint_copy(sizeof(jlong), false, &entry, "arrayof_jlong_disjoint_arraycopy");
    StubRoutines::_arrayof_jlong_arraycopy          = generate_conjoint_copy(sizeof(jlong), false, entry, NULL, "arrayof_jlong_arraycopy");

    // oop
    StubRoutines::_oop_disjoint_arraycopy         = generate_disjoint_copy(sizeof(oop), true, &entry, "oop_disjoint_arraycopy");
    StubRoutines::_oop_arraycopy                  = generate_conjoint_copy(sizeof(oop), true, entry, &entry_oop_arraycopy, "oop_arraycopy");
    StubRoutines::_arrayof_oop_disjoint_arraycopy = generate_disjoint_copy(sizeof(oop), true, &entry, "arrayof_oop_disjoint_arraycopy");
    StubRoutines::_arrayof_oop_arraycopy          = generate_conjoint_copy(sizeof(oop), true, entry, NULL, "arrayof_oop_arraycopy");

    StubRoutines::_oop_disjoint_arraycopy_uninit         = StubRoutines::_oop_disjoint_arraycopy;
    StubRoutines::_oop_arraycopy_uninit                  = StubRoutines::_oop_arraycopy;
    StubRoutines::_arrayof_oop_disjoint_arraycopy_uninit = StubRoutines::_arrayof_oop_disjoint_arraycopy;
    StubRoutines::_arrayof_oop_arraycopy_uninit          = StubRoutines::_arrayof_oop_arraycopy;

    StubRoutines::_checkcast_arraycopy = NULL;
    StubRoutines::_unsafe_arraycopy    = NULL;
    StubRoutines::_generic_arraycopy   = NULL;
  }

  // -------------------------------------------------------------
  // Phase 1 (generate_initial) and Phase 2 (generate_all)
  // -------------------------------------------------------------
  void generate_initial() {
    StubRoutines::_forward_exception_entry = generate_forward_exception();
    StubRoutines::_call_stub_entry = generate_call_stub(StubRoutines::_call_stub_return_address);
    StubRoutines::_catch_exception_entry = generate_catch_exception();
    StubRoutines::_throw_StackOverflowError_entry =
      generate_throw_exception("StackOverflowError throw_exception",
                               CAST_FROM_FN_PTR(address, SharedRuntime::throw_StackOverflowError));
  }

  void generate_all() {
    StubRoutines::_verify_oop_subroutine_entry = generate_verify_oop();
    StubRoutines::_throw_AbstractMethodError_entry =
      generate_throw_exception("AbstractMethodError throw_exception",
                               CAST_FROM_FN_PTR(address, SharedRuntime::throw_AbstractMethodError));
    StubRoutines::_throw_IncompatibleClassChangeError_entry =
      generate_throw_exception("IncompatibleClassChangeError throw_exception",
                               CAST_FROM_FN_PTR(address, SharedRuntime::throw_IncompatibleClassChangeError));
    StubRoutines::_throw_NullPointerException_at_call_entry =
      generate_throw_exception("NullPointerException at call throw_exception",
                               CAST_FROM_FN_PTR(address, SharedRuntime::throw_NullPointerException_at_call));

    generate_arraycopy_stubs();

    generate_safefetch("SafeFetch32", sizeof(int), &StubRoutines::_safefetch32_entry,
                                                   &StubRoutines::_safefetch32_fault_pc,
                                                   &StubRoutines::_safefetch32_continuation_pc);
    generate_safefetch("SafeFetchN", sizeof(intptr_t), &StubRoutines::_safefetchN_entry,
                                                       &StubRoutines::_safefetchN_fault_pc,
                                                       &StubRoutines::_safefetchN_continuation_pc);
  }

 public:
  StubGenerator(CodeBuffer* code, bool all) : StubCodeGenerator(code) {
    if (all) {
      generate_all();
    } else {
      generate_initial();
    }
  }
}; // end class declaration

void StubGenerator_generate(CodeBuffer* code, bool all) {
  StubGenerator g(code, all);
}
