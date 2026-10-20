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

#include "precompiled.hpp"
#include "runtime/os.hpp"
#include "vm_version_riscv.hpp"

#ifdef TARGET_OS_FAMILY_linux
# include <sys/auxv.h>
#endif

int VM_Version::_cpuFeatures = 0;
const char* VM_Version::_features_str = "";

void VM_Version::determine_features() {
  int features = CPU_RV64I | CPU_RV64M | CPU_RV64A | CPU_RV64F | CPU_RV64D | CPU_RV64C;

#ifdef TARGET_OS_FAMILY_linux
# ifdef AT_HWCAP
  unsigned long hwcap = getauxval(AT_HWCAP);
  if (hwcap != 0) {
    features = CPU_RV64I;
    if (hwcap & (1UL << ('M' - 'A'))) features |= CPU_RV64M;
    if (hwcap & (1UL << ('A' - 'A'))) features |= CPU_RV64A;
    if (hwcap & (1UL << ('F' - 'A'))) features |= CPU_RV64F;
    if (hwcap & (1UL << ('D' - 'A'))) features |= CPU_RV64D;
    if (hwcap & (1UL << ('C' - 'A'))) features |= CPU_RV64C;
  }
# endif
#endif

  _cpuFeatures = features;
  _features_str = "RV64IMAFDC";
}

void VM_Version::initialize() {
  determine_features();

  if (FLAG_IS_DEFAULT(UsePopCountInstruction)) {
    FLAG_SET_DEFAULT(UsePopCountInstruction, false);
  }

#ifdef COMPILER2
  // RV64GC base ISA has no hardware conditional move or prefetch instruction
  if (ConditionalMoveLimit > 0) {
    FLAG_SET_DEFAULT(ConditionalMoveLimit, 0);
  }
  if (FLAG_IS_DEFAULT(AllocatePrefetchStyle)) {
    FLAG_SET_DEFAULT(AllocatePrefetchStyle, 0);
  }
  if (FLAG_IS_DEFAULT(MaxVectorSize)) {
    FLAG_SET_DEFAULT(MaxVectorSize, 0);
  }
  if (FLAG_IS_DEFAULT(UseMulAddIntrinsic)) {
    FLAG_SET_DEFAULT(UseMulAddIntrinsic, true);
  }
  if (FLAG_IS_DEFAULT(UseMultiplyToLenIntrinsic)) {
    FLAG_SET_DEFAULT(UseMultiplyToLenIntrinsic, true);
  }
#endif // COMPILER2

  _initialized = true;
}
