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

