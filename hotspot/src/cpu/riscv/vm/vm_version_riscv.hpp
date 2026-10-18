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

#ifndef CPU_RISCV_VM_VM_VERSION_RISCV_HPP
#define CPU_RISCV_VM_VM_VERSION_RISCV_HPP

#include "runtime/globals_extension.hpp"
#include "runtime/vm_version.hpp"

class VM_Version : public Abstract_VM_Version {
 public:
  enum Feature_Flag {
    CPU_RV64I = 1 << 0,
    CPU_RV64M = 1 << 1,
    CPU_RV64A = 1 << 2,
    CPU_RV64F = 1 << 3,
    CPU_RV64D = 1 << 4,
    CPU_RV64C = 1 << 5
  };

 protected:
  static int _cpuFeatures;
  static const char* _features_str;

 public:
  static void initialize();
  static void determine_features();
  static const char* cpu_features() { return _features_str; }
  static bool supports_rv64m() { return (_cpuFeatures & CPU_RV64M) != 0; }
  static bool supports_rv64a() { return (_cpuFeatures & CPU_RV64A) != 0; }
  static bool supports_rv64f() { return (_cpuFeatures & CPU_RV64F) != 0; }
  static bool supports_rv64d() { return (_cpuFeatures & CPU_RV64D) != 0; }
  static bool supports_rv64c() { return (_cpuFeatures & CPU_RV64C) != 0; }
  static bool supports_cx8()   { return true; }
};

#endif // CPU_RISCV_VM_VM_VERSION_RISCV_HPP
