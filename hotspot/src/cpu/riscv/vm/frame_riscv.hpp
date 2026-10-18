/*
 * Copyright (c) 1997, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_RISCV_VM_FRAME_RISCV_HPP
#define CPU_RISCV_VM_FRAME_RISCV_HPP

#include "runtime/synchronizer.hpp"
#include "utilities/top.hpp"

// A frame represents a physical stack frame (an activation).
// Layout of RISC-V stack frame:
//
//   [expression stack      ] * <- sp
//   ...
//   [old frame pointer     ]   <- fp = link()
//   [return pc             ]
//   ...

 public:
  enum {
    pc_return_offset                                 =  0,
    // All frames
    link_offset                                      = -2,
    return_addr_offset                               = -1,
    sender_sp_offset                                 =  0,

#ifndef CC_INTERP
    // Interpreter frames
    interpreter_frame_result_handler_offset          =  1, // for native calls only
    interpreter_frame_oop_temp_offset                =  0, // for native calls only

    interpreter_frame_sender_sp_offset               = -3,
    // outgoing sp before a call to an invoked method
    interpreter_frame_last_sp_offset                 = interpreter_frame_sender_sp_offset - 1,
    interpreter_frame_method_offset                  = interpreter_frame_last_sp_offset - 1,
    interpreter_frame_mdx_offset                     = interpreter_frame_method_offset - 1,
    interpreter_frame_cache_offset                   = interpreter_frame_mdx_offset - 1,
    interpreter_frame_locals_offset                  = interpreter_frame_cache_offset - 1,
    interpreter_frame_bcx_offset                     = interpreter_frame_locals_offset - 1,
    interpreter_frame_initial_sp_offset              = interpreter_frame_bcx_offset - 1,

    interpreter_frame_monitor_block_top_offset       = interpreter_frame_initial_sp_offset,
    interpreter_frame_monitor_block_bottom_offset    = interpreter_frame_initial_sp_offset,
#endif // CC_INTERP

    // Entry frames
    entry_frame_after_call_words                     = 35,
    entry_frame_call_wrapper_offset                  = -10,

    arg_reg_save_area_bytes                          =  0,
    metadata_words                                   =  2,
    frame_alignment                                  = 16,
    native_frame_initial_param_offset                =  0
  };

  intptr_t ptr_at(int offset) const {
    return *ptr_at_addr(offset);
  }

  void ptr_at_put(int offset, intptr_t value) {
    *ptr_at_addr(offset) = value;
  }

 private:
  intptr_t*   _fp; // frame pointer
  intptr_t*   _unextended_sp;
  void adjust_unextended_sp();

  intptr_t* ptr_at_addr(int offset) const {
    return (intptr_t*) addr_at(offset);
  }

#ifdef ASSERT
  static void verify_deopt_original_pc(   nmethod* nm, intptr_t* unextended_sp, bool is_method_handle_return = false);
  static void verify_deopt_mh_original_pc(nmethod* nm, intptr_t* unextended_sp) {
    verify_deopt_original_pc(nm, unextended_sp, true);
  }
#endif

 public:
  // Constructors
  frame(intptr_t* sp, intptr_t* fp, address pc);
  frame(intptr_t* sp, intptr_t* unextended_sp, intptr_t* fp, address pc);
  frame(intptr_t* sp, intptr_t* fp);

  void init(intptr_t* sp, intptr_t* fp, address pc);

  intptr_t* fp() const { return _fp; }

  inline address* sender_pc_addr() const;
  inline address  sender_pc_maybe_signed() const;
  inline address* native_param_addr(int idx) const;

  // expression stack tos if we are nested in a java call
  intptr_t* interpreter_frame_last_sp() const;

  static void update_map_with_saved_link(RegisterMap* map, intptr_t** link_addr);

#ifndef CC_INTERP
  void interpreter_frame_set_last_sp(intptr_t* sp);
#endif // CC_INTERP

#endif // CPU_RISCV_VM_FRAME_RISCV_HPP
