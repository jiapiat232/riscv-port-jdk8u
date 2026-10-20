# OpenJDK 8u 新架构移植指南：从零构建基础 libjvm.so 的适配矩阵与踩坑实录 (RISC-V 移植实战)

> 本文基于 OpenJDK 8u 原生 RISC-V 64 位（`riscv64`）移植工程（Phase 1 架构基线闭环），系统性总结在 HotSpot JVM 中从零引入一个全新 CPU 架构、成功编译并链接出最小可用 `libjvm.so` 核心动态库所需的**全部适配矩阵**，以及在此过程中遭遇的**重大编译排错、链接未决符号与跨语言标准深坑**。

---

## 目录
- [一、移植总览与架构闭环目标（Bootstrap Target）](#一移植总览与架构闭环目标bootstrap-target)
- [二、HotSpot 架构解耦与文件组织模型](#二hotspot-架构解耦与文件组织模型)
- [三、基础 libjvm.so 编译链接的八大核心适配矩阵](#三基础-libjvmso-编译链接的八大核心适配矩阵)
  - [3.1 构建系统与平台三元组识别（Build System & Target Triplet）](#31-构建系统与平台三元组识别build-system--target-triplet)
  - [3.2 全局架构宏与头文件分发路由（Global Macros & Dispatch Router）](#32-全局架构宏与头文件分发路由global-macros--dispatch-router)
  - [3.3 CPU 寄存器体系与 VMReg 映射（Registers & VMReg System）](#33-cpu-寄存器体系与-vmreg-映射registers--vmreg-system)
  - [3.4 弱内存模型、原子操作与屏障（Memory Model & Atomics）](#34-弱内存模型原子操作与屏障memory-model--atomics)
  - [3.5 操作系统/CPU 粘合与信号机制（OS/CPU Glue & Signal Handling）](#35-操作系统cpu-粘合与信号机制oscpu-glue--signal-handling)
  - [3.6 栈帧结构与跨语言调用约定（Frame & Calling Conventions）](#36-栈帧结构与跨语言调用约定frame--calling-conventions)
  - [3.7 汇编器基座与 Native 指令识别（Assembler & NativeInst）](#37-汇编器基座与-native-指令识别assembler--nativeinst)
  - [3.8 C1/C2 编译器骨架与 ADLC 机器描述（Compiler Hooks & ADLC）](#38-c1c2-编译器骨架与-adlc-机器描述compiler-hooks--adlc)
- [四、构建过程中踩坑全景与深度根因剖析（Pitfalls & Root Causes）](#四构建过程中踩坑全景与深度根因剖析pitfalls--root-causes)
  - [4.1 坑点 1：全局寄存器别名导致数学库变量命名冲突（zero 污染）](#41-坑点-1全局寄存器别名导致数学库变量命名冲突zero-污染)
  - [4.2 坑点 2：C++98 语言标准铁律与现代编译器（GCC 9+）语法碰撞](#42-坑点-2c98-语言标准铁律与现代编译器gcc-9语法碰撞)
  - [4.3 坑点 3：非 PCH 编译单元头文件包含断裂（Incomplete Type VM_Version）](#43-坑点-3非-pch-编译单元头文件包含断裂incomplete-type-vm_version)
  - [4.4 坑点 4：ADLC 链接未决符号（CallStubImpl 与 HandlerImpl）](#44-坑点-4adlc-链接未决符号callstubimpl-与-handlerimpl)
  - [4.5 坑点 5：Serviceability Agent（SA）调试库强耦合构建阻塞](#45-坑点-5serviceability-agentsa调试库强耦合构建阻塞)
  - [4.6 坑点 6：Linux 动态加载器 ELF 识别与大页魔数缺损](#46-坑点-6linux-动态加载器-elf-识别与大页魔数缺损)
  - [4.7 坑点 7：解释器预分配代码缓存尺寸缺失（InterpreterCodeSize）](#47-坑点-7解释器预分配代码缓存尺寸缺失interpretercodesize)
  - [4.8 坑点 8：JNI/JVM 平台性能监控扩展桩缺损（vm_version_ext）](#48-坑点-8jnijvm-平台性能监控扩展桩缺损vm_version_ext)
  - [4.9 坑点 9：C2 编译器指令选择器与寄存器掩码未决符号](#49-坑点-9c2-编译器指令选择器与寄存器掩码未决符号)
  - [4.10 坑点 10：类作用域成员函数遮蔽全局寄存器别名陷阱（frame::fp() vs ::fp）](#410-类作用域成员函数遮蔽全局寄存器别名陷阱framefp-vs-fp)
  - [4.11 坑点 11：共享层嵌入式头文件架构接入（interpreter_<arch>.hpp）](#411-共享层嵌入式头文件架构接入interpreter_archhpp)
  - [4.12 坑点 12：SharedRuntime::c_calling_convention 的 4 参数签名规范](#412-sharedruntimec_calling_convention-的-4-参数签名规范)
  - [4.13 坑点 13：C++ 名字查找与隐藏规则（Name Hiding）：派生类宏汇编重载隐藏基类方法](#413-c-名字查找与隐藏规则name-hiding派生类宏汇编重载隐藏基类方法)
  - [4.14 坑点 14：共享层与架构层字节码模板符号冲突（Multiple Definition of TemplateTable Methods）](#414-共享层与架构层字节码模板符号冲突multiple-definition-of-templatetable-methods)
  - [4.15 坑点 15：JDK 8u 与现代 JDK 分支回边计数器（Backedge Counter）掩码模型鸿沟](#415-jdk-8u-与现代-jdk-分支回边计数器backedge-counter掩码模型鸿沟)
  - [4.16 坑点 16：call_VM 践踏 xmethod（t6）导致解释器派发 SIGSEGV 与 psABI 寄存器保全体系](#416-call_vm-践踏-xmethodt6导致解释器派发-sigsegv-与-psabi-寄存器保全体系)
  - [4.17 坑点 17：宏汇编 push/pop 混淆 C 栈（sp）与表达式栈（esp）破坏 16 字节对齐](#417-宏汇编-pushpop-混淆-c-栈sp与表达式栈esp破坏-16-字节对齐)
  - [4.18 坑点 18：remove_activation 桩实现导致 synchronized 方法锁泄漏与断言失败](#418-remove_activation-桩实现导致-synchronized-方法锁泄漏与断言失败)
  - [4.19 坑点 19：Interpreter::_remove_activation_entry 缺失导致异常栈展开 SIGSEGV](#419-interpreter_remove_activation_entry-缺失导致异常栈展开-sigsegv)
  - [4.20 坑点 20：ABI 传参寄存器映射表（g_INTArgReg / g_FPArgReg）的声明与定义解耦](#420-abi-传参寄存器映射表g_intargreg--g_fpargreg的声明与定义解耦)
  - [4.21 坑点 21：Native JNI Signature Handler 浮点位图与动态栈传参陷阱](#421-native-jni-signature-handler-浮点位图与动态栈传参陷阱)
  - [4.22 坑点 22：异常安全性与 Thread::do_not_unlock_if_synchronized 的时序生命周期](#422-异常安全性与-threaddo_not_unlock_if_synchronized-的时序生命周期)
  - [4.23 跨代际移植架构鸿沟实战指南：JDK 1.8 规范与 JDK 19 GA / 上游主线实现的三位一体适配方法论](#423-跨代际移植架构鸿沟实战指南jdk-18-规范与-jdk-19-ga--上游主线实现的三位一体适配方法论)
- [五、新架构移植工程化核对清单（Step-by-Step Checklist）](#五新架构移植工程化核对清单step-by-step-checklist)

---

## 一、移植总览与架构闭环目标（Bootstrap Target）

在将 OpenJDK 移植到一个全新的硬件架构时，很多初学者容易陷入一个误区：**“试图一次性写完整套解释器、汇编器和 JIT，然后再去执行 make”**。这种做法在 HotSpot 这种数百万行 C++ 代码的庞然大物面前几乎必然导致灾难——数以千计的语法错误、类型不匹配和未决符号会彻底掩盖真实的核心问题。

### 1.1 为什么必须以“基础 libjvm.so 编译链接”作为第一里程碑？
在 OpenJDK 移植工程学中，最科学的路线是**分阶段建立防线（Milestone Defense）**：
1. **Phase 0**：打通跨平台容器化交叉编译环境与 QEMU 模拟执行链路。
2. **Phase 1**：**构建基础 `libjvm.so` 闭环（Linkable Minimal Baseline）**。本阶段的目标是**不要求 JVM 能执行 Java 字节码，但必须能让 GCC/Clang 顺利完成全部编译单元，并通过 GNU `ld` 链接器成功输出 ELF 动态库文件**。
3. **Phase 2**：实现核心调用桩（`call_stub`）与模板解释器，跑通 `java -version`。
4. **Phase 3-4**：实现 C1 客户端编译器与 C2 服务端编译器。

打通 Phase 1 的核心价值在于：
- **锁定类型系统与数据结构大小**：验证栈帧大小、寄存器位宽、指针宽度在目标架构与 OS 上的定义是否自洽；
- **打通头文件包含路由网络**：HotSpot 的条件编译路由极为繁复，Phase 1 能暴露出所有缺失的平台专有头文件；
- **确认动态链接与导出符号表**：确保 `JNI_CreateJavaVM`、`JVM_InitProperties` 等关键符号能够被外部 `java` 启动器正常解析与绑定。

### 1.2 目标 ELF 规格验收标准
一个成功的 Phase 1 编译链接产物应当具备以下技术指标：
- **文件格式**：`ELF 64-bit LSB shared object, UCB RISC-V, version 1 (SYSV), dynamically linked`
- **ABI 标志**：`Flags: 0x5, RVC, double-float ABI`（对应 RV64GC 标准 LP64D 调用规范）
- **体积指标**：包含完整调试符号（`slowdebug`）时约 20MB ~ 25MB；剥离符号后约 8MB ~ 12MB。
- **导出符号**：`nm -D libjvm.so` 必须可检索到 `T JNI_CreateJavaVM` 等 HotSpot 核心入口点。

---

## 二、HotSpot 架构解耦与文件组织模型

HotSpot 采用非常清晰的物理目录隔离来支撑多架构、多操作系统：

```
hotspot/
├── make/linux/makefiles/             # Linux 平台 Makefile（编译器选项、系统链接库）
│   ├── defs.make                     # 全局平台宏定义与 SA 调试库控制
│   ├── riscv64.make                  # [新架构] 架构特定 CFLAGS/OPT_CFLAGS
│   └── sa.make / saproc.make         # SA 代理调试器构建逻辑
└── src/
    ├── cpu/<arch>/vm/                # [架构纯逻辑] 寄存器、指令集、汇编器、栈帧、C1/C2
    │   └── riscv/vm/
    ├── os_cpu/<os>_<arch>/vm/        # [OS与架构粘合] 信号处理、ucontext 捕获、原子操作、内存屏障
    │   └── linux_riscv/vm/
    ├── os/<os>/vm/                   # [OS通用逻辑] Linux 内存映射、动态库加载、时钟
    │   └── linux/vm/
    └── share/vm/                     # [JVM核心共享代码] 运行时、GC、解释器通用层、C2 优化器
```

### 核心分发机制：宏路由（Macro Routing）
HotSpot 在共享目录 `src/share/vm/` 中**绝对禁止**直接硬编码包含特定架构文件（例如禁止在 `frame.cpp` 中直接 `#include "frame_riscv.hpp"`）。
它统一采用基于宏开关的分发路由模式：

```cpp
// 在 src/share/vm/runtime/frame.hpp 中：
#ifdef TARGET_ARCH_x86
# include "frame_x86.hpp"
#endif
#ifdef TARGET_ARCH_aarch64
# include "frame_aarch64.hpp"
#endif
#ifdef TARGET_ARCH_riscv
# include "frame_riscv.hpp"
#endif
```

这意味着引入一个新架构，必须在**共享层的全部路由点**中注入对应的 `TARGET_ARCH_<arch>` 与 `TARGET_OS_ARCH_<os>_<arch>` 分支。

---


## 三、基础 libjvm.so 编译链接的八大核心适配矩阵

要让 HotSpot 的全量 C++ 源码在全新的 RISC-V 目标架构下成功编译并通过静态链接器产出 `libjvm.so`，必须构建以下八大子系统的最小必要适配集合（Minimal Adaptation Set）：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       八大核心适配矩阵（Phase 1 编译链接基线）                   │
├───────────────────────────────┬─────────────────────────────────────────────┤
│ 1. 构建系统与平台三元组识别     │ autoconf platform.m4, platform_riscv64,      │
│                               │ riscv64.make, defs.make, sa 解耦             │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 2. 全局架构宏与头文件分发路由   │ macros.hpp, globals.hpp, frame.hpp 等 20+   │
│                               │ share/vm 分发头文件路由注入                   │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 3. CPU 寄存器体系与 VMReg 映射 │ register_riscv.hpp/cpp, vmreg_riscv.hpp/cpp │
│                               │ 32个通用寄存器、32个浮点寄存器与HotSpot保留寄存器 │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 4. 弱内存模型、原子操作与屏障   │ orderAccess_linux_riscv.inline.hpp (RVWMO)  │
│                               │ atomic_linux_riscv.inline.hpp (AMO+LR/SC)   │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 5. 操作系统/CPU 粘合与信号机制 │ os_linux_riscv.cpp/hpp, thread_linux_riscv  │
│                               │ ucontext_t 寄存器提取、缺页信号修补、栈帧回溯 │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 6. 栈帧结构与跨语言调用约定     │ frame_riscv.hpp/inline.hpp, jniTypes_riscv  │
│                               │ javaFrameAnchor, LP64D 16字节对齐调用约定    │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 7. 汇编器基座与 Native 指令识别 │ assembler_riscv, macroAssembler_riscv       │
│                               │ nativeInst_riscv (安全点轮询与指令探测桩)     │
├───────────────────────────────┼─────────────────────────────────────────────┤
│ 8. C1/C2 编译器骨架与 ADLC 描述│ riscv.ad (CallStubImpl, HandlerImpl),       │
│                               │ c1_FrameMap, c1_LIR, vm_version_ext         │
└───────────────────────────────┴─────────────────────────────────────────────┘
```

---

### 3.1 构建系统与平台三元组识别（Build System & Target Triplet）

OpenJDK 8u 的构建系统分为两级：顶层 Autoconf（负责环境探测与生成 spec.gmk）和底层 HotSpot Makefile 体系。

#### 1. 顶层平台探测：`common/autoconf/platform.m4`
必须注册目标架构的 canonical triple，使 `./configure --openjdk-target=riscv64-unknown-linux-gnu` 能够正确识别：
```m4
riscv64)
  VAR_CPU=riscv64
  VAR_CPU_ARCH=riscv
  VAR_CPU_BITS=64
  VAR_CPU_ENDIAN=little
  ;;
```
- `VAR_CPU=riscv64`：对应 Makefile 中产物子目录名（如 `linux_riscv64_compiler2`）。
- `VAR_CPU_ARCH=riscv`：对应 `hotspot/src/cpu/` 下的子目录名 `riscv`。
- `VAR_CPU_ENDIAN=little`：声明小端序。

#### 2. HotSpot 平台配置：`hotspot/make/linux/platform_riscv64`
定义 HotSpot 专用的底层架构宏与包含路径：
```makefile
os_family = linux
arch = riscv
arch_model = riscv64
platform = linux-riscv64

sys_defs = -DRISCV -DRISCV64 -D_LP64=1
```

#### 3. 架构专有编译开关：`hotspot/make/linux/makefiles/riscv64.make`
```makefile
# 启用小端序支持
CFLAGS += -DVM_LITTLE_ENDIAN

# 针对 fdlibm 数学库的编译优化级别控制
ifneq ($(FDLIBM_CFLAGS),)
  OPT_CFLAGS/sharedRuntimeTrig.o = $(OPT_CFLAGS/SPEED) $(FDLIBM_CFLAGS)
  OPT_CFLAGS/sharedRuntimeTrans.o = $(OPT_CFLAGS/SPEED) $(FDLIBM_CFLAGS)
else
  OPT_CFLAGS/sharedRuntimeTrig.o = $(OPT_CFLAGS/NOOPT)
  OPT_CFLAGS/sharedRuntimeTrans.o = $(OPT_CFLAGS/NOOPT)
endif

OPT_CFLAGS/compactingPermGenGen.o = -O1
```

---

### 3.2 全局架构宏与头文件分发路由（Global Macros & Dispatch Router）

#### 1. 核心架构宏定义：`hotspot/src/share/vm/utilities/macros.hpp`
HotSpot 全量代码使用宏开关进行代码隔离。若未定义 `RISCV_ONLY`，共享层中所有架构保护块都将无法生效：
```cpp
#if defined(RISCV) || defined(RISCV64)
#ifndef RISCV
#define RISCV
#endif
#define RISCV_ONLY(code) code
#define NOT_RISCV(code)
#else
#define RISCV_ONLY(code)
#define NOT_RISCV(code) code
#endif

#ifdef RISCV64
#define RISCV64_ONLY(code) code
#define NOT_RISCV64(code)
#else
#define RISCV64_ONLY(code)
#define NOT_RISCV64(code) code
#endif
```

#### 2. 共享层分发头文件列表
必须在以下 20 多个共享层头文件中补充 `#ifdef TARGET_ARCH_riscv` 与 `#ifdef TARGET_OS_ARCH_linux_riscv` 分支：
- `hotspot/src/share/vm/runtime/globals.hpp` -> 路由到 `globals_riscv.hpp` 与 `globals_linux_riscv.hpp`
- `hotspot/src/share/vm/runtime/frame.hpp` 与 `frame.inline.hpp` -> 路由到 `frame_riscv.hpp` / `frame_riscv.inline.hpp`
- `hotspot/src/share/vm/runtime/stubRoutines.hpp` -> 路由到 `nativeInst_riscv.hpp` 与 `stubRoutines_riscv.hpp`
- `hotspot/src/share/vm/runtime/javaFrameAnchor.hpp` -> 路由到 `javaFrameAnchor_riscv.hpp`
- `hotspot/src/share/vm/runtime/jniTypes.hpp` -> 路由到 `jniTypes_riscv.hpp`
- `hotspot/src/share/vm/runtime/os.hpp` -> 路由到 `os_linux_riscv.hpp`
- `hotspot/src/share/vm/runtime/thread.hpp` -> 路由到 `thread_linux_riscv.hpp`
- `hotspot/src/share/vm/runtime/threadLocalStorage.hpp` -> 路由到 `threadLS_linux_riscv.hpp`
- `hotspot/src/share/vm/runtime/icache.hpp` -> 路由到 `icache_riscv.hpp`
- `hotspot/src/share/vm/runtime/vmStructs.cpp` -> 路由到 `vmStructs_riscv.hpp` 与 `vmStructs_linux_riscv.hpp`
- `hotspot/src/share/vm/interpreter/templateInterpreter.hpp` -> 路由到 `templateInterpreter_riscv.hpp`
- `hotspot/src/share/vm/asm/assembler.hpp` -> 路由到 `assembler_riscv.hpp`
- `hotspot/src/share/vm/asm/macroAssembler.hpp` -> 路由到 `macroAssembler_riscv.hpp`
- `hotspot/src/share/vm/asm/codeBuffer.hpp` -> 路由到 `codeBuffer_riscv.hpp`
- `hotspot/src/share/vm/code/relocInfo.hpp` -> 路由到 `relocInfo_riscv.hpp`
- `hotspot/src/share/vm/c1/` 下全部 `c1_*.hpp` -> 路由到 `c1_*_riscv.hpp`

---

### 3.3 CPU 寄存器体系与 VMReg 映射（Registers & VMReg System）

寄存器系统是 JVM 内部调用约定、寄存器分配器（RA）和垃圾回收（GC 栈扫描）的基石。

#### 1. 通用寄存器（GPR）与浮点寄存器（FPR）定义：`register_riscv.hpp`
- 映射标准 RISC-V 32 个通用寄存器 `x0` - `x31`，以及标准 ABI 别名：
  - `ra` (x1, 返回地址), `sp` (x2, 栈指针), `gp` (x3), `tp` (x4)
  - `t0`-`t6` (临时寄存器)
  - `s0`/`fp` (x8, 帧指针), `s1`-`s11` (被调用者保存寄存器)
  - `a0`-`a7` (参数/返回值寄存器)
- **HotSpot 架构保留专用寄存器**：
  - `xthread = s7 (x23)`：恒常持有当前 Java 线程对象的 `JavaThread*` 指针，禁止分配器分配给普通变量！
  - `xheapbase = s11 (x27)`：压缩指针模式下持有堆基址 `NarrowOop` 基地址。

#### 2. VMReg 虚拟寄存器统一映射：`vmreg_riscv.hpp/cpp`
HotSpot 的垃圾回收器、解优化（Deoptimization）与 OopMap 无法直接操作硬件寄存器，必须使用统一的 `VMReg` 标量索引：
```cpp
// 64 位系统下，一个 64 位通用寄存器需要分配 2 个 32-bit VMReg 槽位
static const int ConcreteRegisterImpl::max_gpr = 32 * 2;
static const int ConcreteRegisterImpl::max_fpr = 32 * 2;
static const int ConcreteRegisterImpl::number_of_registers = max_gpr + max_fpr;

inline VMReg RegisterImpl::as_VMReg() {
  return VMRegImpl::as_VMReg((encoding() << 1));
}
inline VMReg FloatRegisterImpl::as_VMReg() {
  return VMRegImpl::as_VMReg((encoding() << 1) + ConcreteRegisterImpl::max_gpr);
}
```

---

### 3.4 弱内存模型、原子操作与屏障（Memory Model & Atomics）

RISC-V 采用非常宽松的弱内存模型（RVWMO - RISC-V Weak Memory Ordering）。HotSpot 内部的锁、并发 GC 和 Volatile 变量完全依托于此处的实现。

#### 1. 内存屏障映射：`orderAccess_linux_riscv.inline.hpp`
严格按照 RVWMO 规范映射：
- `OrderAccess::acquire()` -> `asm volatile("fence r, rw" : : : "memory");`
- `OrderAccess::release()` -> `asm volatile("fence rw, w" : : : "memory");`
- `OrderAccess::fence()` -> `asm volatile("fence rw, rw" : : : "memory");`
- `OrderAccess::storestore()` -> `asm volatile("fence w, w" : : : "memory");`
- `OrderAccess::loadload()` -> `asm volatile("fence r, r" : : : "memory");`

#### 2. 原子操作铁律：`atomic_linux_riscv.inline.hpp`
- **优先使用标准单指令 AMO（Atomic Memory Operation）**：
  - `Atomic::add` -> `amoadd.w.aqrl` / `amoadd.d.aqrl`
  - `Atomic::xchg` -> `amoswap.w.aqrl` / `amoswap.d.aqrl`
- **对于无法单指令完成的比较并交换（CAS）**：使用 `lr.w.aqrl` / `sc.w.rl` 循环包裹实现。

---

### 3.5 操作系统/CPU 粘合与信号机制（OS/CPU Glue & Signal Handling）

这是 `libjvm.so` 能够正常加载、处理异常信号与实现安全点（Safepoint）的核心。

#### 1. 关键文件：`os_linux_riscv.cpp/hpp`
- **信号处理函数 `JVM_handle_linux_signal`**：
  - 拦截 Linux 的 `SIGSEGV`（空指针、安全点页面探测缺页）、`SIGBUS`、`SIGFPE`（除零）、`SIGILL`。
- **从 `ucontext_t` 中提取与修补 PC/SP/FP**：
  在 Linux RISC-V 64 上，上下文结构体存储在 `mcontext_t` 的 `__gregs` 数组中：
  ```cpp
  address os::Linux::ucontext_get_pc(ucontext_t * uc) {
    return (address)uc->uc_mcontext.__gregs[REG_PC];
  }
  void os::Linux::ucontext_set_pc(ucontext_t * uc, address pc) {
    uc->uc_mcontext.__gregs[REG_PC] = (greg_t)pc;
  }
  intptr_t* os::Linux::ucontext_get_sp(ucontext_t * uc) {
    return (intptr_t*)uc->uc_mcontext.__gregs[REG_SP];
  }
  intptr_t* os::Linux::ucontext_get_fp(ucontext_t * uc) {
    return (intptr_t*)uc->uc_mcontext.__gregs[REG_S0];
  }
  ```
- **全局安全点页面探测判定（Safepoint Polling Page）**：
  当 GC 触发全局安全点时，会将专门的内存页置为不可访问，线程读取该页引发 `SIGSEGV`。`os_linux_riscv.cpp` 必须通过：
  ```cpp
  if (sig == SIGSEGV && os::is_poll_address((address)info->si_addr)) {
    // 识别为安全点轮询探测，将 PC 重定向到安全点 Stub 入口，返回 1 消耗信号
  }
  ```

---

### 3.6 栈帧结构与跨语言调用约定（Frame & Calling Conventions）

#### 1. 栈帧布局：`frame_riscv.hpp/inline.hpp`
- **对齐规范**：RISC-V LP64 ABI 强制要求栈指针 `sp` 恒常保持 **16 字节对齐**。
- **栈帧指针链接**：
  - `fp` (`s0`) 指向上一个栈帧的帧底；
  - `fp - 1`（即 `fp - 8` 字节处）保存当前帧的返回地址 `pc`；
  - `fp - 2`（即 `fp - 16` 字节处）保存上一个栈帧的 `fp`。
- `frame::sender()`：利用上述链表关系，向上回溯调用者的栈帧，实现栈打印（Stack Trace）与异常展开（Stack Unwinding）。

#### 2. Java 线程与 C/C++ 锚点：`javaFrameAnchor_riscv.hpp`
当 Java 调用 JNI 本地方法或 C++ Runtime 函数时，必须通过 `JavaFrameAnchor` 保存最后一次有效的 `_last_Java_sp`、`_last_Java_fp` 与 `_last_Java_pc`，确保 GC 线程在并发扫描栈时能准确识别栈的 Java/Native 边界。

---

### 3.7 汇编器基座与 Native 指令识别（Assembler & NativeInst）

#### 1. 汇编器与宏汇编器：`assembler_riscv.hpp`, `macroAssembler_riscv.hpp`
- `AbstractAssembler` / `Assembler`：提供机器码发射器基类 `emit_int32(int32_t x)`。
- `MacroAssembler`：封装高层宏操作，如 64 位大常数跨指令加载 `movptr(Register dst, intptr_t val)`（通过 `lui` + `addi` + `slli` + `addi`... 拼接），调用运行时函数 `call_VM`。

#### 2. 机器码修补与探测门面：`nativeInst_riscv.hpp`
HotSpot 在运行时会动态修补已编译代码（Code Patching，例如将普通调用 patch 为 IC 调用，或者将 Safepoint 检查 patch 为陷阱指令）。必须在此文件中定义最小识别函数：
- `NativeInstruction::is_safepoint_poll()`：判断当前指令是否是 `ld zr, (safepoint_addr)`。
- `NativeCall::is_call_before(address return_address)`：根据返回地址回推前一条指令是否为 `jal` / `jalr`。

---

### 3.8 C1/C2 编译器骨架与 ADLC 机器描述（Compiler Hooks & ADLC）

即使在 Phase 1 不启用 JIT 编译执行，HotSpot 的默认编译目标（`compiler2` 或 `tiered`）在编译 `libjvm.so` 时也会编译 C1 编译器（Client Compiler）和 C2 编译器（Server Compiler）。因此必须提供最小骨架头文件与 ADLC 语法描述：

#### 1. C1 客户端编译器适配：`c1_*_riscv.hpp`
- `c1_Defs_riscv.hpp`：定义 C1 虚拟机的寄存器数量、浮点寄存器数量与传参寄存器映射。
- `c1_FrameMap_riscv.hpp`：定义寄存器分配表（`_cpu_rinfo`），向寄存器分配器声明哪些是临时寄存器、哪些被保留。

#### 2. C2 架构描述语言：`riscv.ad` 与 `riscv64.ad`
HotSpot 会使用自带的 `adlc` 工具将 `riscv.ad` 编译为巨大的 C++ 源代码（如 `ad_riscv.cpp`, `dfa_riscv.cpp`）。
若要在 Phase 1 通过链接，`riscv.ad` 必须声明：
- 架构基本管道与寄存器类定义（`reg_class`）；
- 核心指令模式桩（Instructions）；
- **链接关键实现桩**：`CallStubImpl::emit_trampoline_stub` 与 `HandlerImpl::emit_exception_handler`。


## 四、构建过程中踩坑全景与深度根因剖析（Pitfalls & Root Causes）

在打通基础 `libjvm.so` 编译链接的过程中，我们遭遇了一系列经典的、跨越 C++ 标准、平台 ABI、构建系统耦合与共享层命名污染的重大坑点。以下对这些典型坑点进行全景复盘与根因推演。

---

### 4.1 坑点 1：全局寄存器别名导致数学库变量命名冲突（zero 污染）

#### 1. 错误现场
在编译 `sharedRuntimeTrans.o` 时，GCC 抛出严重类型冲突错误：
```text
hotspot/src/share/vm/runtime/sharedRuntimeTrans.cpp:46:15: error: conflicting declaration ‘static double zero = 0.0;’
hotspot/src/cpu/riscv/vm/register_riscv.hpp:52:23: note: previous declaration as ‘const Register zero’
```

#### 2. 深度根因分析
- HotSpot 8u 采用 C++98 设计，命名空间隔离极弱。为了方便编写汇编宏，各个架构通常会在 `register_<arch>.hpp` 中定义全局寄存器常量（如 `ra`, `sp`）。
- 初版 RISC-V 端口代码中定义了：
  ```cpp
  const Register zero = x0;
  ```
- 然而，HotSpot 共享目录下的 `sharedRuntimeTrans.cpp` 与 `sharedRuntimeTrig.cpp` 是直接将早期的 C 语言数学库 `fdlibm`（Freely Distributable LIBM）翻译而来的底层三角函数/浮点转换实现。该文件中包含大量全局静态数学常数声明：
  ```cpp
  static double zero = 0.0;
  static double one  = 1.0;
  ```
- 当 `sharedRuntimeTrans.cpp` 通过头文件链展开包含 `register_riscv.hpp` 时，全局作用域下的 `zero` 发生直接碰撞：同一个标识符在同一个命名空间内先后被声明为 `Register`（底层结构体指针）和 `double`，引发不可恢复的编译硬错误！

#### 3. 解决准则
**坚决禁止在全局作用域定义高频通用词汇作为寄存器别名！**
- 将全局寄存器别名严格限制为 `zr`（同理杜绝 `one`、`top` 等词汇）：
  ```cpp
  // register_riscv.hpp
  // 移除: const Register zero = x0;
  const Register zr = x0; // 统一使用 zr 作为 x0 的全局别名
  ```
- 若特定汇编宏需要使用 `zero`，仅在宏汇编器成员函数的作用域内局部声明，坚决不向全局命名空间泄漏。

---

### 4.2 坑点 2：C++98 语言标准铁律与现代编译器（GCC 9+）语法碰撞

#### 1. 错误现场
将高版本（如 JDK 11 / JDK 17）已有的 RISC-V 端口实现直接反向移植到 JDK 8u 时，GCC 疯狂报错：
```text
error: ‘auto’ does not name a type
error: ‘nullptr’ was not declared in this scope
error: ‘constexpr’ needed for in-class initialization
error: range-based ‘for’ loops are not allowed in C++98 mode
```

#### 2. 深度根因分析
- JDK 11 及以后已全面拥抱 C++11/C++14。但 OpenJDK 8u 的原生编译参数显式锁定了 `-std=gnu++98`：
  ```makefile
  # hotspot/make/linux/makefiles/gcc.make
  CXXFLAGS += -std=gnu++98
  ```
- 即使现代工具链（如 Ubuntu 20.04 默认的 GCC 9.4 或 GCC 10）完全支持现代 C++ 特性，只要传入了 `-std=gnu++98`，编译器就会作为致命语法错误直接拒绝编译。

#### 3. 规避与降级矩阵
所有移植到 JDK 8u 的代码必须无条件执行 C++98 语法降级：
| 现代 C++ 特性 (JDK 11+) | JDK 8u 兼容替代方案 | 规范说明 |
| :--- | :--- | :--- |
| `nullptr` | `NULL` | 严禁使用 `nullptr`，HotSpot 8u 统一用 `NULL` |
| `auto x = expr;` | 显式类型 `Register x = expr;` | 必须显式声明类型 |
| `constexpr int N = 10;` | `const int N = 10;` 或 `enum { N = 10 };` | 类内常量使用枚举或静态只读常量 |
| `for (auto elem : list)` | `for (int i = 0; i < len; i++)` | 采用传统下标或指针递增遍历 |
| `<cstdint>` 中的 `uint64_t` | `julong` 或 `uint64_t` (由 globalDefinitions 声明) | 优先使用 HotSpot 内置跨平台标量类型 |

---

### 4.3 坑点 3：非 PCH 编译单元头文件包含断裂（Incomplete Type VM_Version）

#### 1. 错误现场
在编译某些未开启 PCH（Precompiled Headers）或依赖关系特殊的编译单元（如 `threadLocalAllocBuffer.cpp`）时报错：
```text
In file included from hotspot/src/share/vm/runtime/atomic.inline.hpp:42,
                 from hotspot/src/share/vm/memory/threadLocalAllocBuffer.hpp:30:
hotspot/src/os_cpu/linux_riscv/vm/orderAccess_linux_riscv.inline.hpp: In member function ‘static void OrderAccess::fence()’:
error: incomplete type ‘VM_Version’ used in nested name specifier
error: ‘os::is_MP’ was not declared in this scope
```

#### 2. 深度根因分析
- HotSpot 默认在构建中生成巨型预编译头文件（`precompiled.hpp`），其中包含了绝大多数运行时头文件。如果在 PCH 环境下，编译能偶然通过；
- 但在无 PCH 模式或独立构建单元中，头文件的包含是线性的：`threadLocalAllocBuffer.hpp` 仅仅需要原子操作，因此直接包含了 `atomic.inline.hpp` -> `orderAccess_linux_riscv.inline.hpp`。
- 在 `orderAccess` 或 `atomic` 中，为了判断处理器特性（例如是否支持多核内存屏障或特定原子指令），调用了 `VM_Version::features()` 或 `os::is_MP()`。此时 `vm_version.hpp` 尚未被引入，编译器将 `VM_Version` 判定为不完整类型（Incomplete Type）！

#### 3. 解决准则
**严格落实自包含头文件（Self-Contained Header）原则**：
在平台专有的内联头文件顶部，显式导入所有直接引用的前置运行时定义，绝不能侥幸依赖 PCH 的隐式传递：
```cpp
// orderAccess_linux_riscv.inline.hpp 与 atomic_linux_riscv.inline.hpp
#include "runtime/os.hpp"
#include "vm_version_riscv.hpp"
```

---

### 4.4 坑点 4：ADLC 链接未决符号（CallStubImpl 与 HandlerImpl）

#### 1. 错误现场
HotSpot 编译成功产生所有 `.o` 目标文件后，在最终链接 `libjvm.so` 阶段，GNU `ld` 报错中止：
```text
undefined reference to `CallStubImpl::emit_trampoline_stub(MacroAssembler&, int, address)'
undefined reference to `HandlerImpl::emit_exception_handler(CodeBuffer&)'
```

#### 2. 深度根因分析
- HotSpot C2 服务端编译器使用 ADLC（Architecture Description Language Compiler）解析平台 `.ad` 文件（如 `riscv.ad`），自动生成匹配器（Matcher）和指令发射逻辑。
- ADLC 在生成代码时，假定底层架构必然具备**方法调用蹦床跳跃（Trampoline）**和**异常处理代码发射器（Exception Handler）**的实现类：`CallStubImpl` 与 `HandlerImpl`。
- 若初学者仅仅编写了普通的指令匹配规约，而未在 `.ad` 文件的 `source` 块中提供上述类的具体实现，ADLC 生成的代码在编译时能够通过声明，但在链接时找不到函数体，导致整个 JVM 动态库无法链接。

#### 3. 解决准则
在 `hotspot/src/cpu/riscv/vm/riscv.ad` 的 `source` 块中补齐基础桩实现：
```cpp
// hotspot/src/cpu/riscv/vm/riscv.ad
source %{
void CallStubImpl::emit_trampoline_stub(MacroAssembler &_masm, int offset, address target) {
  // Phase 1 基础跳转发射桩
}

int HandlerImpl::emit_exception_handler(CodeBuffer &cbuf) {
  // Phase 1 基础异常跳转桩
  return 0;
}
%}
```

---

### 4.5 坑点 5：Serviceability Agent（SA）调试库强耦合构建阻塞

#### 1. 错误现场
在 HotSpot 顶层构建脚本调用 `make all_debug` 时，构建在 SA 目录停滞并报出致命错误：
```text
make[6]: *** No rule to make target 'sa-jdi.jar'. Stop.
// 或在编译 libsaproc.so 时:
fatal error: asm/ptrace.h: No such file or directory
error: ‘struct user_regs_struct’ has no member named ‘pc’
```

#### 2. 深度根因分析
- Serviceability Agent（SA）是 HotSpot 自带的跨进程自省与内存转储分析库，产物包含 Java 层的 `sa-jdi.jar` 和本地动态库 `libsaproc.so`。
- `libsaproc.so` 通过 Linux 底层的 `ptrace` 系统调用直接读取目标 JVM 进程的寄存器现场，因此它极度深度耦合 Linux 内核中特定架构的 `struct user_regs_struct` 定义。
- 此外，SA 在 Java 语言层还需要一套映射该 CPU 全部寄存器的 Java 类（如 `sun.jvm.hotspot.debugger.riscv64.RISCV64ThreadContext`）。
- **在新架构移植初期，Java 类库尚不能运行，强行编译 SA 是绝对的死胡同。**

#### 3. 解决准则
参考 OpenJDK 官方对 `zero` 和 `ia64` 架构的处理前例，在 Phase 1 编译期通过 Makefile 排除 `riscv` 的 SA 构建：
1. **修改 `hotspot/make/linux/makefiles/saproc.make`**：
   ```makefile
   ifneq ($(wildcard $(AGENT_DIR)),)
   # 将 riscv 排除出 BUILDLIBSAPROC
   ifneq ($(filter-out ia64 zero riscv,$(SRCARCH)),)
     BUILDLIBSAPROC = $(LIBSAPROC)
   endif
   endif
   ```
2. **修改 `hotspot/make/linux/makefiles/sa.make`**：
   ```makefile
   all: 
   	if [ -d $(AGENT_DIR) -a "$(SRCARCH)" != "ia64" \
                -a "$(SRCARCH)" != "zero" \
                -a "$(SRCARCH)" != "riscv" ] ; then \
   	   $(MAKE) -f sa.make $(GENERATED)/sa-jdi.jar; \
   	fi
   ```
3. **修改 `hotspot/make/linux/makefiles/defs.make`**：
   ```makefile
   ADD_SA_BINARIES/riscv =
   ```
将 SA 解耦并延迟到 Phase 5，确保核心 `libjvm.so` 能够单飞构建。

---

### 4.6 坑点 6：Linux 动态加载器 ELF 识别与大页魔数缺损

#### 1. 错误现场
编译 `hotspot/src/os/linux/vm/os_linux.cpp` 时报预编译检查失败：
```text
error: Method os::dll_load requires that one of following is defined:
IA32, AMD64, IA64, __sparc, __powerpc__, ARM, S390, ALPHA, MIPS, MIPSEL, PARISC, M68K, AARCH64, LOONGARCH
```

#### 2. 深度根因分析
HotSpot 在 Linux 平台加载动态链接库（`os::dll_load`）时，为了防止 32 位与 64 位混用或跨架构错误加载，会主动读取 `.so` 文件的 ELF Header，校验 `e_machine` 字段。JDK 8u 原生代码中没有收录 RISC-V 的机器识别码。

#### 3. 解决准则
在 `hotspot/src/os/linux/vm/os_linux.cpp` 中完整注入 RISC-V 标识：
```cpp
#ifndef EM_RISCV
#define EM_RISCV      243               /* RISC-V 官方 ELF 机器识别码 */
#endif

// 注册至架构兼容表
static const arch_t arch_array[]={
  ...
  {EM_RISCV,       EM_RISCV,   ELFCLASS64, ELFDATA2LSB, (char*)"RISC-V 64"},
};

// 运行时自身架构标记
#elif  (defined RISCV) || (defined RISCV64)
  static  Elf32_Half running_arch_code=EM_RISCV;

// 大页尺寸定义
size_t os::Linux::find_large_page_size() {
#ifndef ZERO
  large_page_size = IA32_ONLY(4 * M) AMD64_ONLY(2 * M) ... RISCV_ONLY(2 * M);
#endif
```

---

### 4.7 坑点 7：解释器预分配代码缓存尺寸缺失（InterpreterCodeSize）

#### 1. 错误现场
`hotspot/src/share/vm/interpreter/abstractInterpreter.cpp` 编译报错：找不到 `InterpreterCodeSize`。

#### 2. 深度根因分析
HotSpot 的解释器启动时需要向 JVM CodeCache 预分配一块内存生成字节码桩。这个常量由各架构的 `templateInterpreter_<arch>.hpp` 自行指定。若未提供该常量，共享层初始化逻辑将无法完成静态数组尺寸评估。

#### 3. 解决准则
在 `hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.hpp` 中提供标准常量：
```cpp
public:
  static const int InterpreterCodeSize = 256 * 1024; // 256 KB 足够承载 RV64GC 基础模板解释器
```

---

### 4.8 坑点 8：JNI/JVM 平台性能监控扩展桩缺损（vm_version_ext）

#### 1. 错误现场
`hotspot/src/os/linux/vm/os_perf_linux.cpp` 在包含 `vm_version_ext_riscv.hpp` 时报头文件缺失或类 `VM_Version_Ext` 方法未实现。

#### 2. 深度根因分析
Linux 平台的系统性能数据采集器（Performance Counters / JMX OS MBean）通过 `VM_Version_Ext` 类读取底层的 CPU 核心数、物理插槽数和硬件特性描述。缺失该实现会导致整个 `libjvm.so` 失去 JMX 性能自省能力。

#### 3. 解决准则
创建最小骨架类：
- `hotspot/src/cpu/riscv/vm/vm_version_ext_riscv.hpp`
- `hotspot/src/cpu/riscv/vm/vm_version_ext_riscv.cpp`
提供基本的 CPU 描述信息实现：
```cpp
const char* VM_Version_Ext::cpu_model_description() {
  return "RISC-V 64-bit (RV64GC)";
}
int VM_Version_Ext::number_of_threads() {
  return os::processor_count();
}
```

---

### 4.9 坑点 9：架构寄存器别名与共享层全局基本类型冲突（s1/s2/s4/s8 重定义）

#### 1. 错误现场
在引入 RISC-V Callee-saved 寄存器别名（`s0~s11`）后，编译 `precompiled.hpp.gch` 报严重重定义错误：
```text
hotspot/src/cpu/riscv/vm/register_riscv.hpp:153:16: error: ‘RegisterImpl* const s1’ redeclared as different kind of entity
hotspot/src/share/vm/utilities/globalDefinitions.hpp:366:16: note: previous declaration ‘typedef jbyte s1’
hotspot/src/cpu/riscv/vm/register_riscv.hpp:162:16: error: ‘RegisterImpl* const s2’ redeclared as different kind of entity
hotspot/src/share/vm/utilities/globalDefinitions.hpp:367:16: note: previous declaration ‘typedef jshort s2’
hotspot/src/cpu/riscv/vm/register_riscv.hpp:164:16: error: ‘RegisterImpl* const s4’ redeclared as different kind of entity
hotspot/src/share/vm/utilities/globalDefinitions.hpp:368:16: note: previous declaration ‘typedef jint s4’
hotspot/src/cpu/riscv/vm/register_riscv.hpp:168:16: error: ‘RegisterImpl* const s8’ redeclared as different kind of entity
hotspot/src/share/vm/utilities/globalDefinitions.hpp:369:16: note: previous declaration ‘typedef jlong s8’
```

#### 2. 深度根因分析
- 在 RISC-V 标准 ABI 中，Callee-saved 通用寄存器被称为 `s0~s11`（对应物理寄存器 `x8, x9, x18~x27`）。
- 然而，HotSpot 共享层在 `hotspot/src/share/vm/utilities/globalDefinitions.hpp` 中声明了面向 Class 文件字节码解析的全局整数基础类型：
  `typedef jbyte s1; typedef jshort s2; typedef jint s4; typedef jlong s8;`
  `typedef jubyte u1; typedef jushort u2; typedef juint u4; typedef julong u8;`
- 如果在 `register_riscv.hpp` 中直接声明 `const Register s1 = x9; const Register s2 = x18; ...`，会导致 C++ 命名空间出现全局符号碰撞，编译器无法区分 `s1` / `s2` / `s4` / `s8` 到底是一个类型名还是一个指针常量变量！

#### 3. 解决准则与官方上游（JDK 19+）对齐演进
**架构层全局寄存器常量严禁使用裸 `s1`, `s2`, `s4`, `s8`！**
- **官方主线对齐规范（向社区推送准则）**：
  查验 OpenJDK 官方主线（JEP 422，`src/hotspot/cpu/riscv/`），官方架构师的设计理念更加纯粹：
  1. `register_riscv.hpp` 严格只声明 `x0~x31` 物理常量编号，根本不定义 `s0~s11`，亦不引入 `xs0~xs11`；
  2. 唯有具备 HotSpot 专用强语义的寄存器赋予领域名字（`xthread = x23`, `xheapbase = x27`, `xlocals = x24`, `xbcp = x22` 等）；
  3. 运行时桩（如 `forward_exception`）中若需临时使用 Callee-saved 寄存器，官方统一直接书写物理编号（如 `x9`）；
  4. 为方便未来向社区开源推送与降低 Code Review 摩擦，本工程已完成对齐重构：剔除 `xs0~xs11`，全面遵循官方物理编号与语义别名规范。

- **跨版本架构移植铁律（JDK 8u vs JDK 19+ 上游）**：
  上游 RISC-V 端口于 JDK 19+ 正式合入主线，而 JDK 8u 与现代 JDK 存在极为深刻的底层架构鸿沟，后续移植实现绝不可机械照搬上游代码：
  1. **GC 内存屏障体系**：现代 JDK 全面下沉至 `BarrierSetAssembler`，且 C2 广泛推行 G1 Barrier Late Expand（延迟展开优化）；而在 JDK 8u 中，解释器与汇编器直接耦合 `CardTableModRefBS` 与 `G1SATBCardTableModRefBS`（如 `g1_write_barrier_pre` / `post` 手写汇编调用），绝无 Late Expand 框架；
  2. **字节码常量池解析**：现代 JDK 引入了 Project Leyden 的 `ResolvedFieldEntry` / `ResolvedMethodEntry`；而 JDK 8u 严格依托 `ConstantPoolCacheEntry` 槽位偏移计算（`f1_byte`, `f2_byte`, `is_vfinal`）；
  3. **安全点轮询机制**：现代 JDK 采用每线程独立的 `Thread::polling_page_offset()` 内存握手；而 JDK 8u 严格使用单一全局保护页 `os::get_polling_page()` 触发 SIGSEGV。
  **开发铁律**：以 JDK 8u 的 AArch64 架构机制与接口规范为“骨架”，以最新 JDK 官方 RISC-V 指令序列与寄存器约定为“肌肉”，二者精准对齐融合。

---

### 4.10 类作用域成员函数遮蔽全局寄存器别名陷阱（`frame::fp()` vs `::fp`）

#### 1. 现象
在实现 `frame_riscv.cpp` 的 `frame::update_map_with_saved_link` 时：
```cpp
void frame::update_map_with_saved_link(RegisterMap* map, intptr_t** link_addr) {
  map->set_location(fp->as_VMReg(), (address) link_addr);
}
```
GCC 报错：
`error: invalid use of member function 'intptr_t* frame::fp() const' (did you forget the '()' ?)`
`error: base operand of '->' is not a pointer`

#### 2. 根因
在 `class frame` 成员函数作用域内，编译器在非限定名称查找（Unqualified Name Lookup）时，类成员函数 `frame::fp()` 的优先级高于全局作用域中声明的寄存器常量 `const Register fp = x8;`。编译器将 `fp` 解析为了成员函数指针，进而拒绝调用 `->as_VMReg()`。

#### 3. 解决准则
在 `class frame` 或任何拥有同名成员方法的类（例如拥有 `sp()`, `fp()`, `pc()` 方法的类）内部访问全局硬件寄存器常量时，必须显式附加全局作用域限定符：
```cpp
void frame::update_map_with_saved_link(RegisterMap* map, intptr_t** link_addr) {
  map->set_location(::fp->as_VMReg(), (address) link_addr);
  map->set_location(::fp->as_VMReg()->next(), (address) link_addr);
}
```

---

### 4.11 共享层嵌入式头文件架构接入（`interpreter_<arch>.hpp`）

#### 1. 现象
在编译 `frame_riscv.cpp` 时报错：
`error: 'expr_offset_in_bytes' is not a member of 'Interpreter'`

#### 2. 根因
HotSpot 解释器核心类 `AbstractInterpreter` / `Interpreter` 将部分平台特有的表达式栈和局部变量偏移计算函数放在 `interpreter_<arch>.hpp` 中，并通过 `interpreter.hpp` 在类定义的末尾直接嵌入包含：
```cpp
#ifdef TARGET_ARCH_x86
# include "interpreter_x86.hpp"
#endif
```
若遗漏在 `interpreter.hpp` 中挂载 `#ifdef TARGET_ARCH_riscv` 并提供 `interpreter_riscv.hpp`，`Interpreter::expr_offset_in_bytes` 等静态函数便不复存在。

#### 3. 解决准则
新架构必须创建 `hotspot/src/cpu/<arch>/vm/interpreter_<arch>.hpp`，实现：
- `static int expr_offset_in_bytes(int i) { return stackElementSize * i; }`
- `static int expr_index_at(int i)        { return stackElementWords * i; }`
- `static int local_index_at(int i)       { return stackElementWords * i; }`
并在 `hotspot/src/share/vm/interpreter/interpreter.hpp` 中添加包含路由。

---

### 4.12 `SharedRuntime::c_calling_convention` 的 4 参数签名规范

#### 1. 现象
在 `sharedRuntime_riscv.cpp` 中若按照常见的 3 参数实现：
`int SharedRuntime::c_calling_convention(const BasicType *sig_bt, VMRegPair *regs, int total_args_passed)`
会导致链接报错 `no declaration matches`。

#### 2. 根因
OpenJDK 8u 的共享层头文件 `sharedRuntime.hpp` 中，C 调用约定接口为了支持某些需要辅助寄存器数组的特殊架构（如部分 SPARC/PowerPC 变体），固定声明了 4 个参数：
`static int c_calling_convention(const BasicType *sig_bt, VMRegPair *regs, VMRegPair *regs2, int total_args_passed);`

#### 3. 解决准则
RISC-V 端口必须严格匹配 4 参数原型，并在函数头部断言 `regs2 == NULL`：
```cpp
int SharedRuntime::c_calling_convention(const BasicType *sig_bt,
                                         VMRegPair *regs,
                                         VMRegPair *regs2,
                                         int total_args_passed) {
  assert(regs2 == NULL, "not needed on RISC-V");
  ...
}
```

---

### 4.13 C++ 名字查找与隐藏规则（Name Hiding）：派生类宏汇编重载隐藏基类方法

#### 1. 现象
在 `MacroAssembler` 中增加了立即数版本的 `void subw(Register Rd, Register Rn, int64_t decrement, Register tmp = t0)` 后，在 `templateTable_riscv.cpp` 中调用三寄存器版本 `__ subw(x10, x11, x10);` 报硬错误：
`error: cannot convert ‘Register’ to ‘int64_t’ for argument ‘3’ to ‘void MacroAssembler::subw(Register, Register, int64_t, Register)’`

#### 2. 根因
C++ 名字查找规则规定：当派生类声明了一个与基类同名的成员函数时，不论其参数列表是否不同，派生类的同名函数都会在无限定查找中**完全隐藏（Hide）**基类中的所有同名重载版本，基类的 `Assembler::subw(Register, Register, Register)` 不会被纳入候选集合。

#### 3. 上游对比（为什么高版本 JDK 19 没有这个问题？）
查阅上游 JDK 19 GA 源码（`src/hotspot/cpu/riscv/assembler_riscv.hpp:3071`）可知：
上游将 `add`、`addw`、`sub`、`subw` 等立即数计算伪指令直接声明在**基类 `Assembler` 类内部**，与三寄存器版本的原生指令（`INSN(subw, c_subw, _subw);`）同属于基类 `Assembler` 的同一重载集（Overload Set）。
在调用 `__ subw(...)` 时，由于派生类 `MacroAssembler` 没有重新声明同名重载，编译器在基类 `Assembler` 作用域内同时匹配到了三寄存器与立即数两个版本，因此不会发生跨类的名字隐藏。
而早期向 JDK 8u 移植时，立即数版本被误声明在派生类 `MacroAssembler` 中，导致无意中屏蔽了基类重载。

#### 4. 解决准则
两种标准解决方案：
1. 规范对齐上游：直接将立即数伪指令声明在基类 `Assembler` 中；
2. 若在派生类扩展重载：在派生类声明中显式通过 `using` 声明将基类的同名重载集引入派生类作用域：
```cpp
// macroAssembler_riscv.hpp
using Assembler::subw;
void subw(Register Rd, Register Rn, int64_t decrement, Register tmp = t0);
```

---

### 4.14 共享层与架构层字节码模板符号冲突（Multiple Definition of TemplateTable Methods）

#### 1. 现象
全量编译完成进入 `Linking vm...` 时，GNU ld 报错：
`multiple definition of TemplateTable::float_cmp(int); templateTable.o: first defined here`
`multiple definition of TemplateTable::_goto(); templateTable.o: first defined here`
包括 `double_cmp(int)`, `goto_w()`, `jsr()`, `jsr_w()` 共 6 个例程重复定义。

#### 2. 根因
在 JDK 19+ 上游主线中，部分字节码模板的平台独立实现经过了重构或分派转移。但在 OpenJDK 8u 中，共享层 `hotspot/src/share/vm/interpreter/templateTable.cpp` 已经提供了这 6 个字节码的平台独立实现（它们内部调用平台专有的 `float_cmp(bool, int)` 和 `branch(bool, bool)`）。如果架构层源码重写了它们，就会导致目标文件链接冲突。

#### 3. 解决准则
架构层 `templateTable_<arch>.cpp` 严禁定义这 6 个方法，只需实现底层平台相关的 `float_cmp(bool is_float, int unordered_result)` 与 `branch(bool is_jsr, bool is_wide)`。

---

### 4.15 JDK 8u 与现代 JDK 分支回边计数器（Backedge Counter）掩码模型鸿沟

#### 1. 现象
移植上游模板解释器的 `branch` 循环回边统计逻辑时，报错：
`error: ‘backedge_mask_offset’ is not a member of ‘MethodData’ / ‘MethodCounters’`

#### 2. 根因
现代 JDK 将分层编译回边计数器的动态掩码移入了 `MethodData` 与 `MethodCounters` 实例中，由 VM 统一维护。而在 OpenJDK 8u 中，解释器回边通知的掩码是基于编译选项即时计算的常数：
`int mask = ((1 << Tier0BackedgeNotifyFreqLog) - 1) << InvocationCounter::count_shift;`

#### 3. 解决准则
在 `branch(bool is_jsr, bool is_wide)` 中采用 JDK 8u 规范，计算编译期常数掩码，调用 `InterpreterMacroAssembler::increment_mask_and_jump(Address, int, int, Register, Register, bool, Label*)` 进行位与溢出检测与 OSR 跳转。

---

### 4.16 `call_VM` 践踏 `xmethod`（`t6`）导致解释器派发 SIGSEGV 与 psABI 寄存器保全体系

#### 1. 现场崩溃拓扑与现象
在 Phase 2 联调初期执行 `./docker/qemu-java.sh -Xint -version` 时，JVM 在刚刚启动并加载基础类（如 `java.lang.Object`、`java.lang.String`）并进入首批字节码解释执行时，瞬间爆发致命 `SIGSEGV`。
GDB 抓取的汇编现场停留在字节码派发前夕：
```
Program received signal SIGSEGV, Segmentation fault.
0x0000003ff7a1c8f4 in TemplateInterpreterGenerator::generate_normal_entry ()
(gdb) info registers
ra             0x3ff7a1c8d0        0x3ff7a1c8d0
sp             0x3fffd53a40        0x3fffd53a40
fp             0x3fffd53a60        0x3fffd53a60
t6 (xmethod)   0x0000000000000000  0x0                <--- 灾难：xmethod 被清零或覆盖为垃圾
(gdb) x/i $pc
=> 0x3ff7a1c8f4: ld  t0, 16(t6)                       <--- 解引用 NULL 崩溃！
```
该指令试图通过 `xmethod`（`Method*`）读取内部结构 `Method::const_offset()` 以获取 `ConstMethod*`。由于 `t6` 成为非法地址，触发内核缺页保护并终止进程。

#### 2. 根因剖析：RISC-V psABI 寄存器分类体系与 HotSpot 专有寄存器的冲突
在标准 RISC-V LP64D ELF psABI 中，32 个通用整数寄存器的调用规范划分如下：
* **Callee-saved（被调用者保存）**：`s0/fp`, `s1`, `s2~s11`（共 12 个）。任何 C/C++ 函数若需使用这些寄存器，必须在函数序言（prologue）中将其压栈，并在尾声（epilogue）中弹栈恢复；
* **Caller-saved / Scratch（调用者保存 / 临时）**：`ra`, `t0~t6`, `a0~a7`（共 16 个）。任何 C/C++ 函数均可自由修改、破坏这些寄存器的值，调用者若需保全其内容，必须在发起 `jalr` 之前自行压栈；
* **保留 / 专用**：`zero`, `sp`, `gp`, `tp`。

然而，HotSpot 解释器为了极致的执行性能，建立了一套专有的固定寄存器分配规范：
```
┌──────────────┬──────────────────┬───────────────────────────────────────────┐
│ 物理寄存器   │ HotSpot 别名     │ 角色定位与 psABI 属性                      │
├──────────────┼──────────────────┼───────────────────────────────────────────┤
│ x8 / s0      │ fp               │ Java 栈帧基址指针（Callee-saved）         │
│ x2 / sp      │ sp               │ 机器执行栈顶指针（ABI 保留，16 字节对齐）  │
│ x20 / s4     │ esp              │ Java 表达式栈顶指针（Callee-saved）       │
│ x27 / s11    │ xdispatch        │ 解释器全局派发表基址（Callee-saved）       │
│ x26 / s10    │ xthread          │ 当前 JavaThread* 句柄（Callee-saved）     │
│ x25 / s9     │ xbcp             │ 当前字节码指针 BCP（Callee-saved）        │
│ x24 / s8     │ xlocals          │ 当前方法局部变量表基址（Callee-saved）     │
│ x31 / t6     │ xmethod          │ 当前执行的 Method* 句柄（Caller-saved!）   │
└──────────────┴──────────────────┴───────────────────────────────────────────┘
```
**关键冲突在于 `x31`（`t6`）**：
`xmethod` 是 HotSpot 在解释执行循环中高度频繁访问的寄存器，用于读取常量池、异常表和方法属性。但它被映射在 RISC-V psABI 的 Caller-saved 寄存器 `t6` 上！
当解释器执行需要陷入 VM 运行时的字节码（如 `invokevirtual` 首次解析常量池、`new` 分配堆内存、`checkcast` 解析类型）时，宏汇编会通过 `MacroAssembler::call_VM` 调用底层的 C++ 静态函数（例如 `InterpreterRuntime::resolve_get_put`）。
GCC/Clang 在编译这些 C++ 函数时，默认将 `t6` 视为临时草稿寄存器，并在其函数体内随意覆盖；当 C++ 运行时函数返回解释器汇编继续点时，`xmethod` 已经荡然无存！

#### 3. 上游对比与解决准则
对比上游 JDK 19 GA（[`macroAssembler_riscv.cpp:571`](file:///Users/wujiahua/code/jdk19/src/hotspot/cpu/riscv/macroAssembler_riscv.cpp#L571)）与 JDK 8u AArch64（[`macroAssembler_aarch64.cpp:1341`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/macroAssembler_aarch64.cpp#L1341)）：
上游在 `MacroAssembler::call_VM_leaf_base` 的最底层封装处，在发起 native 跳转前，必须显式在机器 C 栈（`sp`）上开辟空间，将可能被 C 代码踩踏的易失专有寄存器安全暂存，并在 call 返回后严格还原：
```cpp
void MacroAssembler::call_VM_leaf_base(address entry_point,
                                       int number_of_arguments,
                                       Label *ret_addr) {
  // 注意：RISC-V psABI 要求 sp 必须时刻保持 16 字节对齐！
  // 保存 t0（暂存器）与 xmethod（t6），恰好占用 2 * wordSize = 16 字节
  subi(sp, sp, 2 * wordSize);
  sd(t0, Address(sp, 0));
  sd(xmethod, Address(sp, wordSize));

  if (ret_addr != NULL) {
    jal(ra, entry_point);
    bind(*ret_addr);
  } else {
    call(entry_point);
  }

  ld(xmethod, Address(sp, wordSize));
  ld(t0, Address(sp, 0));
  addi(sp, sp, 2 * wordSize);
}
```

---

### 4.17 宏汇编 `push`/`pop` 混淆 C 栈（`sp`）与表达式栈（`esp`）破坏 16 字节对齐

#### 1. 现象与断言失败
在模板解释器生成 `checkcast`、`instanceof`、`monitorenter` 等需要暂存操作数栈顶元素（TOSCA）的字节码中，JVM 间歇性抛出断言崩溃：
```
Internal Error (os_linux_riscv.cpp:513), pid=1234, tid=5678
assert(((intptr_t)os::current_stack_pointer() & (StackAlignmentInBytes-1)) == 0) failed: incorrect stack alignment
```
同时在 QEMU 模拟环境中，一旦遇到 Linux 信号处理（如 Safepoint polling 触发的 `SIGSEGV`），glibc 的信号派发跳板因检测到用户栈指针未对齐而直接以 `SIGBUS` 或无效指令终止。

#### 2. 根因剖析：双轨栈体系（Dual-Stack Model）与 psABI 对齐铁律
HotSpot 在 RISC-V 架构上运行着两套完全独立、但交织在同一虚拟内存空间的栈体系：
```
         高地址 (High Address)
        ┌───────────────────────────────────┐
        │  Caller Frame                     │
        ├───────────────────────────────────┤ <--- fp (x8, 栈帧基准)
        │  Sender SP                        │
        │  Return PC                        │
        ├───────────────────────────────────┤
        │  Local Variables (locals)         │
        ├───────────────────────────────────┤
        │  BasicObjectLocks (Monitors)      │
        ├───────────────────────────────────┤
        │  Java Expression Stack (TOS)      │ <--- 8 字节粒度递增/递减
        │  [operand 2]                      │
        │  [operand 1]                      │ <--- esp (x20, 操作数栈顶)
        ├───────────────────────────────────┤
        │  ... [Dynamic Buffer Space] ...   │
        ├───────────────────────────────────┤
        │  Native C Frame / Spill Area      │ <--- sp (x2, 机器执行栈)
        └───────────────────────────────────┘      [强制 16 字节对齐！]
         低地址 (Low Address)
```
1. **Java 表达式栈（Expression Stack）**：
   - 受 `esp`（`x20`）管辖，每次入栈 `push(r)` 执行 `sd r, Address(esp, 0); addi esp, esp, -wordSize`（或对应负向生长逻辑）；
   - 其元素是 Java 操作数（`jint`, `jobject`, `jlong` 等），**允许以 8 字节（`wordSize`）为单位单字移动**，无需保持 16 字节对齐。
2. **Native 机器执行栈（C Execution Stack）**：
   - 受硬件寄存器 `sp`（`x2`）管辖，供 C++ 函数调用、信号上下文（Signal Frame）与叶子汇编桩使用；
   - **RISC-V psABI 规范强制要求：在任何函数调用边界（Function Boundary）及公共执行点，`sp & 0xF == 0`（必须 16 字节双字对齐）**。

在早期移植代码中，作者混淆了两者的概念，将 `MacroAssembler::push(Register)` 错误地实现为了操作 `sp`：
```cpp
// 致命错误的原版实现：
void MacroAssembler::push(Register r) {
  addi(sp, sp, -wordSize); // 减 8 字节！直接破坏 16 字节对齐！
  sd(r, Address(sp, 0));
}
```
当字节码模板中写下 `__ push(atos); __ call_VM(...); __ pop(x13);` 时：
- `push` 导致 `sp` 变成了奇数个 8 字节（`sp % 16 != 0`）；
- 随后的 `call_VM` 触发 C++ 调用，内部触发栈对齐断言崩毁；
- 更严重的是，`pop` 若与 `esp` 不对称，会导致 Java 操作数栈指针彻底错位，读取到的全部是垃圾数据。

#### 3. 解决准则与防线建立
1. **语义收敛**：`MacroAssembler::push(Register/TosState)` 与 `pop` 的职责**严格锁定为 Java 表达式栈 `esp`**；
2. **机器栈操作显式配对**：所有确实需要分配 C 栈的操作（如保存通用寄存器集合、创建 frame-anchor 辅助帧），严禁单字递增，必须统一以 `2 * wordSize`（16 字节）为基本单位：
```cpp
// 正确范式：机器栈操作始终 16 字节对称
void MacroAssembler::push_reg(Register r1, Register r2) {
  subi(sp, sp, 2 * wordSize);
  sd(r1, Address(sp, 0));
  sd(r2, Address(sp, wordSize));
}
void MacroAssembler::pop_reg(Register r1, Register r2) {
  ld(r1, Address(sp, 0));
  ld(r2, Address(sp, wordSize));
  addi(sp, sp, 2 * wordSize);
}
```

---

### 4.18 `remove_activation` 桩实现导致 `synchronized` 方法锁泄漏与断言失败

#### 1. 现场崩溃与断言
当 JVM 完成核心启动类加载并进入 `System.initializeSystemClass` 初始化阶段时，系统内部大量使用线程安全的哈希表 `Hashtable.put`。
程序在第二次调用同一 synchronized 方法时，立即触发死锁检测断言失败：
```
# Internal Error (synchronizer.cpp:244), pid=2105, tid=2105
# assert(lock != mark->locker()) failed: must not re-lock the same lock
#
# Native frames: (J=compiled Java code, j=interpreted, Vv=VM code, C=native code)
# V  [libjvm.so+0x87612c]  ObjectSynchronizer::slow_enter(Handle, BasicLock*, Thread*)+0x84
# V  [libjvm.so+0x8763f0]  ObjectSynchronizer::fast_enter(Handle, BasicLock*, bool, Thread*)+0x120
# j  java.util.Hashtable.put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;+1
```

#### 2. 根因剖析：方法级 Synchronized 与代码块 Monitorenter 的机制鸿沟
在 JVM 规范中，同步机制分为两类：
- **同步代码块（Synchronized Block）**：javac 会在字节码中显式生成 `monitorenter` 和 `monitorexit`，并配合异常表保证任何路径退出均能释放锁；
- **同步方法（Synchronized Method）**：**字节码中没有任何 `monitorenter` 或 `monitorexit` 指令！**
  它的标志位存放在 `Method::access_flags()` 的 `JVM_ACC_SYNCHRONIZED` 中。
  1. 解释器在方法序言（Prologue）中，发现此标志后自动在栈帧中分配一个 `BasicObjectLock` 槽位，并调用 `InterpreterMacroAssembler::lock_object` 将对象的 Mark Word 原子 CAS 替换为指向当前栈帧锁记录（Displaced Mark Word）的指针；
  2. 当方法执行 `ireturn` / `return` 等退出指令时，必须由 `InterpreterMacroAssembler::remove_activation` 自动承担解锁工作！

在早期代码中，`remove_activation` 仅仅被写成了一个 3 行的空桩：
```cpp
// 早期桩实现：
void InterpreterMacroAssembler::remove_activation(...) {
  ld(esp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
  leave();
  andi(sp, esp, -16);
}
```
**这一漏洞导致灾难性后果**：
方法退出时直接销毁了当前的 Java 栈帧，**但对象的 Mark Word 永远停留在“被当前线程在已销毁栈帧地址锁定”的状态！**
当该线程紧接着第二次调用 `Hashtable.put` 时，由于相同的栈深度分配到了完全相同的虚拟内存地址，新栈帧的 `BasicLock*` 地址与对象 Mark Word 中残存的旧 `locker` 物理地址完全一致。HotSpot 判定这是致命的锁重入死锁逻辑漏洞，直接触发 `assert(lock != mark->locker())` 崩溃退出。

#### 3. 解决准则：完整的结构化锁解锁与异常检查流水线
参照 AArch64 与 JDK 19 GA 规范，在 [`interp_masm_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp#L356-L465) 中重构出工业级完整的 `remove_activation` 逻辑：
1. **检查 `Thread::do_not_unlock_if_synchronized` 标志**：若异常处理流程已经代为释放过锁，跳过方法级解锁；
2. **定位首个 Monitor**：在 RISC-V 解释器帧中，方法级同步监视器始终位于：
   `fp + frame::interpreter_frame_initial_sp_offset * wordSize - sizeof(BasicObjectLock)`；
3. **安全释放锁**：调用 `unlock_object(c_rarg1)`，通过 `amo/cas` 指令将对象的 Mark Word 还原；
4. **结构化锁完整性校验（Block-structured Locking Check）**：
   从 `monitor_block_top` 扫描至 `monitor_block_bot`，断言并确保当前栈帧内的所有内联锁（如果有）均已被正常释放；
5. **JVMTI 事件通知**：调用 `notify_method_exit` 确保调试器与性能分析采样工具能够捕获退出点；
6. **最终销毁栈帧**：原子恢复 `sender_sp` 并执行 `leave` 弹出 `fp` 和 `ra`。

---

### 4.19 `Interpreter::_remove_activation_entry` 缺失导致异常栈展开 SIGSEGV

#### 1. 现象与调用栈
当测试用例执行抛出异常逻辑（如 `throw new RuntimeException()` 或通过除以零触发硬件算术异常），且当前 Java 方法没有就地 `catch` 该异常时，虚拟机在执行异常栈展开（Stack Unwinding）过程中抛出严重崩溃：
```
Program received signal SIGSEGV, Segmentation fault.
0x0000000000000000 in ?? ()
(gdb) bt
#0  0x0000000000000000 in ?? ()
#1  0x0000003ff7a21340 in InterpreterRuntime::exception_handler_for_exception (thread=0x3fffa801b0, exception=0x3fffb40108)
#2  0x0000003ff7a1dc88 in TemplateInterpreterGenerator::generate_throw_exception ()
```
寄存器现场显示，程序跳转到了空指针 `0x0`（`jr a0` where `a0 = 0x0`）。

#### 2. 根因剖析：HotSpot 异常跨帧传递契约
在 HotSpot 解释器中，异常处理的控制流模型如下：
```
[当前方法产生异常]
       │
       ▼
__ call_VM(a0, InterpreterRuntime::exception_handler_for_exception, ...)
       │
       ├─────────────────────────────────────────┐
       │ [当前方法包含匹配的 catch 块]             │ [当前方法未捕获该异常]
       ▼                                         ▼
返回该 catch 块的解释器入口地址 (bcp)      返回 Interpreter::remove_activation_entry()
       │                                         │
       ▼                                         ▼
跳转到本地 catch 块恢复执行 (jr a0)        跳转到跨帧展开跳板 (jr a0)
                                                 │
                                                 ▼
                                        【若该指针未初始化即为 NULL！】
                                                 │
                                                 ▼
                                        Crash: jr 0x0 (SIGSEGV)
```
在 RISC-V 端口的初期初始化中，只生成了普通的字节码入口，**完全漏写了 `Interpreter::_remove_activation_entry` 的机器代码生成逻辑！**
导致 `Interpreter::_remove_activation_entry` 成员变量始终为 `NULL`。一旦发生跨帧异常，运行时函数按约定返回该指针，解释器一跃而入 `0x0` 发生段错误。

#### 3. 解决准则
在 [`templateInterpreter_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp#L1196-L1230) 中完整实现该跳板：
```cpp
  Interpreter::_remove_activation_entry = __ pc();

  // 1. 将当前待抛出的 exception oop 暂存至 JavaThread 的 vm_result 字段
  __ pop_ptr(x10);
  __ sd(x10, Address(xthread, JavaThread::vm_result_offset()));

  // 2. 弹出当前帧（remove_activation 会解除方法锁并还原 caller 的 sp/fp/ra）
  __ remove_activation(vtos, false, true, false);

  // 3. 恢复 exception oop 到 x10
  __ get_vm_result(x10, xthread);

  // 4. 调用运行时查找调用者（caller）栈帧中是否有匹配的 handler
  __ subi(sp, sp, 2 * wordSize);
  __ sd(x10, Address(sp, 0));
  __ sd(ra, Address(sp, wordSize));
  __ MacroAssembler::call_VM_leaf(
      CAST_FROM_FN_PTR(address, SharedRuntime::exception_handler_for_return_address),
      xthread, ra);
  __ mv(x11, x10);                  // x11 保存调用者 handler 入口
  __ ld(x10, Address(sp, 0));       // 恢复异常 oop
  __ ld(ra, Address(sp, wordSize)); // 恢复返回地址
  __ addi(sp, sp, 2 * wordSize);

  // 5. 满足调用者 handler 的约定（x13 存放异常发生点的 pc）并跳转
  __ mv(x13, ra);
  __ jr(x11);
```

---

### 4.20 ABI 传参寄存器映射表（`g_INTArgReg` / `g_FPArgReg`）的声明与定义解耦

#### 1. 现象与技术背景
在 `assembler_riscv.hpp` 中新增了传参寄存器数组声明：
```cpp
// assembler_riscv.hpp
extern const Register g_INTArgReg[Argument::n_int_register_parameters_c];
extern const FloatRegister g_FPArgReg[Argument::n_float_register_parameters_c];
```
并在 `assembler_riscv.cpp` 中唯一定义：
```cpp
// assembler_riscv.cpp
const Register g_INTArgReg[Argument::n_int_register_parameters_c] = {
  c_rarg0, c_rarg1, c_rarg2, c_rarg3, c_rarg4, c_rarg5, c_rarg6, c_rarg7
};

const FloatRegister g_FPArgReg[Argument::n_float_register_parameters_c] = {
  c_farg0, c_farg1, c_farg2, c_farg3, c_farg4, c_farg5, c_farg6, c_farg7
};
```

#### 2. 为什么需要这两个全局数组？
在 RISC-V LP64D 标准 psABI 中，函数调用的前 8 个整型参数由 `a0~a7`（`x10~x17`，别名 `c_rarg0~c_rarg7`）传递，前 8 个浮点参数由 `fa0~fa7`（`f10~f17`，别名 `c_farg0~c_farg7`）传递。
在 HotSpot 运行时中，有两处关键位置必须通过动态参数索引（$i = 0..7$）获取对应的硬件寄存器：
1. `InterpreterRuntime::SignatureHandlerGenerator`（`interpreterRT_riscv.cpp`）：当解释器调用本地方法（Native JNI）时，代码生成器动态扫描方法签名的参数列表。对于第 $i$ 个参数，必须通过 `g_INTArgReg[i]` 或 `g_FPArgReg[i]` 以 $O(1)$ 速度直接索引出目标寄存器，将 Java 栈槽数据映射至对应硬件传参寄存器；
2. `AbstractInterpreterGenerator::generate_slow_signature_handler`（`interpreter_riscv.cpp`）：当走慢速签名处理路径时，解释器生成的解包汇编循环需要按下标恢复所有浮点与整型参数寄存器。
由于在循环中参数下标是动态变量，无法硬编码寄存器名称，这两个数组本质上是 **RISC-V C/Java 调用约定中“参数序号 $\to$ 物理寄存器”的常数查找表（Lookup Table）**。

#### 3. 为什么写成 `extern const`（解耦与工程化规范）？
上游 JDK 19 原版代码为了偷懒，直接在头文件 `assembler_riscv.hpp` 中声明并就地初始化了该数组。
虽然在 C++ 中 `const` 对象默认具有内部链接（Internal Linkage），但这会导致：
- 每一个包含了 `assembler_riscv.hpp` 的 `.cpp` 编译单元在目标文件（`.o`）的 `.rodata` 段中都会独立生成一份完全相同的数组数据副本，造成毫无意义的静态存储冗余与符号膨胀；
- 在部分编译器严格模式或跨编译单元取指针比较时，可能因地址不唯一引发细微的 ODR（One Definition Rule）隐患。
在 OpenJDK 8u 端口中，我们将其重构成严格的现代工程规范：头文件仅用 `extern const` 导出符号接口，在 `assembler_riscv.cpp` 中唯一定义并分配存储空间。

---

### 4.21 Native JNI Signature Handler 浮点位图与动态栈传参陷阱

#### 1. 现象
在执行包含本地方法（JNI）调用的 Java 代码（例如涉及 `Double.longBitsToDouble`、`Math.sin` 等浮点计算或 JNI 包装类）时，传递给 native 方法的浮点参数全部变成 `0.0` 或严重混乱的错位值，甚至导致 native 库访问越界崩溃。

#### 2. 根因剖析：RISC-V LP64D 混合传参规则与浮点位图（Float Register Bitmap）
在 RISC-V LP64D 调用规范中，浮点参数与整型参数的寄存器分配遵循**独立计数器（Independent Slotting）**规则：
- 前 8 个浮点参数分别占用 `fa0~fa7`；
- 前 8 个整型/指针参数分别占用 `a0~a7`；
- 当任一类型的寄存器耗尽后，后续参数将连续压入机器栈空间（16 字节对齐）。
这意味着对于签名 `void nativeFoo(int a, double b, int c, double d)`：
- `a` 进入 `a0`；
- `b` 进入 `fa0`（不是占用第 2 个通用寄存器！）；
- `c` 进入 `a1`；
- `d` 进入 `fa1`。

在 HotSpot 解释器中，Native 调用的快速签名处理器是由 `SignatureHandlerGenerator` 动态生成的微型代码块。为了让通用的 native 适配器桩（`interpreter_native_entry`）能够在运行时极速获知“哪些参数需要放入浮点寄存器”，HotSpot 设计了一个 **32 位整型位图（Float Register Bitmap）**：
- 位图第 $k$ 位为 1，表示第 $k$ 个参数为浮点数（单精度 `float` 或双精度 `double`）；
- 位图第 $k$ 位为 0，表示第 $k$ 个参数为整型或对象引用。

在早期实现中，生成的 Signature Handler 未将计算好的位图存入约定槽位，导致 native 调用跳板在执行 `lwu c_rarg3, Address(sp, 9 * wordSize)` 时读到了栈上的随机垃圾，错误地将浮点值加载到了整型寄存器中，导致 ABI 传参全面崩塌。

#### 3. 解决准则
在 [`interpreterRT_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interpreterRT_riscv.cpp) 中严格实现签名位图编码与参数布局：
1. 维护独立的 `_num_int_args`（整型传参寄存器计数）与 `_num_fp_args`（浮点传参寄存器计数）；
2. 遇到 `T_FLOAT` / `T_DOUBLE` 时，若 `_num_fp_args < 8`，则将参数加载至 `g_FPArgReg[_num_fp_args++]`，并置位 `fp_bitmap |= (1 << arg_idx)`；
3. 遇到整型或引用时，若 `_num_int_args < 8`，则将参数加载至 `g_INTArgReg[_num_int_args++]`；
4. 超过 8 个参数后，计算 C 栈偏移（以 8 字节递增，双字在栈上保持对齐），正确执行 `sd` 压栈；
5. 最后将 `fp_bitmap` 写入预定的结果寄存器或栈槽供 Native 桩消费。

---

### 4.22 异常安全性与 `Thread::do_not_unlock_if_synchronized` 的时序生命周期

#### 1. 现象
在复杂异常捕获测试（如一个 synchronized 方法内嵌套 `try { throw new Exception(); } catch (...)`）中，方法正常返回后，JVM 意外抛出 `IllegalMonitorStateException`（非法监视器状态异常）。

#### 2. 根因剖析
在 JDK 8u 规范中，Java 编译器（javac）在处理 synchronized 代码块时，会在异常表（Exception Table）中生成一条兜底的 `any -> finally` 项。
当异常发生时：
1. 若异常在当前方法内部被捕获，异常处理器代码会先执行 `monitorexit` 释放锁，然后进入用户 catch 块；
2. 若当前方法是 synchronized 方法且异常未在本地捕获，JVM 展开栈时会触发退出解锁。
为了防止在栈展开（Stack Unwind）或跨层返回过程中发生**二次解锁（Double Unlock）**，JDK 8u 的 `JavaThread` 维护了一个极度敏感的单字节标志位：
`_do_not_unlock_if_synchronized`（偏移量通过 `JavaThread::do_not_unlock_if_synchronized_offset()` 获取）。
- 当运行时在处理异常栈展开时，若已经显式清理过锁，会将该标志置为 1；
- 解释器的 `remove_activation` 必须**原子性地读取该标志，并在读取后立即将其清零（Reset to 0）**！
如果 `remove_activation` 忽略了该标志，或者只读取却未重置，会导致后续正常调用的方法误以为“不需要解锁”，再次引发锁泄漏与状态断言失败。

#### 3. 解决准则
在 `InterpreterMacroAssembler::remove_activation` 头部：
```cpp
const Address do_not_unlock_if_synchronized(
    xthread, in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
lbu(x13, do_not_unlock_if_synchronized);
sb(zr, do_not_unlock_if_synchronized); // 读后立即清零，确保下次激活独立不受污染

// 仅当标志为 0 时才执行同步监视器释放
bnez(x13, no_unlock);
```

---

### 4.23 跨代际移植架构鸿沟实战指南：JDK 1.8 规范与 JDK 19 GA / 上游主线实现的三位一体适配方法论

#### 1. 为什么不能直接将 JDK 19 / 主线代码简单复制到 JDK 8u？
许多移植者常犯的一个致命错误是：认为上游已有 RISC-V 端口，直接 `cp` 过去修修编译错误就能跑。
在 HotSpot 演进史中，从 JDK 8（2014）到 JDK 19（2022）跨越了近十年，虚拟机内部经历了数次“地壳运动级”的架构重构：
```
┌────────────────────────┬───────────────────────────┬───────────────────────────┐
│ 核心子系统             │ OpenJDK 8u (本项目基线)    │ 现代 JDK (JDK 19 GA / 主线)│
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ GC 屏障系统 (Barriers) │ 硬编码在 MacroAssembler   │ JEP 304 抽象屏障接口集    │
│                        │ (卡表、G1 Pre/Post)       │ (BarrierSetAssembler)     │
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ CMS 垃圾收集器         │ 生产主力 (必须支持)       │ JDK 14 已彻底删除 (零支持)│
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ 偏向锁 (Biased Locking)│ 默认开启 (Mark Word 包含) │ JDK 15 废弃/移除 (无汇编) │
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ 对象头模型 (Headers)   │ 经典两字头 (Mark + Klass) │ Lilliput 紧凑对象头 (单字)│
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ 安全点机制 (Safepoint) │ 全局只读内存轮询页        │ JEP 312 线程局部握手      │
│                        │ (Global Polling Page)     │ (Thread-Local Handshake)  │
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ 栈水印与协程 (Loom)    │ 不存在 (标准物理栈)       │ Stack Watermark Barrier   │
│                        │                           │ Continuation Freeze/Thaw  │
├────────────────────────┼───────────────────────────┼───────────────────────────┤
│ 代码修补 (Self-Modify) │ patch_verified_entry 原地 │ NMethod Entry Barrier     │
│                        │ 覆写跳转/非法指令         │ 轻量原子标志翻转          │
└────────────────────────┴───────────────────────────┴───────────────────────────┘
```
如果盲目照抄高版本代码，会引入大量现代特性依赖（如调用不存在的 `BarrierSetAssembler`、引用已被移除的 `ThreadLocalAllocBuffer` 新字段、甚至触发栈水印屏障崩溃）。

#### 2. 三位一体移植方法论（The Three-Way Porting Methodology）
本项目创立并严格奉行“三位一体”移植开发范式：
```
              【规范与接口之锚】
           OpenJDK 8u AArch64 端口
        (hotspot/src/cpu/aarch64/vm/)
                     │
                     │  定义：JDK 8u 的函数签名、GC 屏障点、
                     │        锁模型、全局 Safepoint 契约
                     ▼
           ┌───────────────────┐
           │ JDK 8u RISC-V 端口│ <====== 我们的目标实现！
           └───────────────────┘
                     ▲
                     │  参考：纯净的 RV64GC 汇编指令模板、
                     │        伪指令展开、寄存器使用规则
                     │
              【指令与汇编之源】
             JDK 19 GA RISC-V 端口
        (/Users/wujiahua/code/jdk19/)
```
- **Step 1（明规范）**：阅读 JDK 8u AArch64 端口中对应功能的实现（如 `MacroAssembler::card_write_barrier_post`）。确定在 JDK 8u 的微架构下，该例程需要操作哪些寄存器、需要满足哪些共享层接口约束；
- **Step 2（采指令）**：查阅 `/Users/wujiahua/code/jdk19/src/hotspot/cpu/riscv` 中最基础、未经后续特性污染的原始 RV64 汇编实现；
- **Step 3（剥离与重组）**：
  - **断然剥离**：凡涉及 Lilliput 紧凑头、Valhalla 扁平对象、Stack Watermark Barrier、Continuation 的高版本代码，一律剔除；
  - **定制重建**：针对上游已删除但 JDK 8u 必需的核心功能（如 CMS 卡表屏障、偏向锁撤销、全局安全点轮询），基于 AArch64 算法逻辑与 RISC-V 原生指令（如 `lr.d/sc.d`、`fence`）完成等价的 RISC-V 原生汇编实现；
- **Step 4（强注释契约）**：
  在所有涉及架构鸿沟的实现中，必须统一打上规范的注释说明：
  ```cpp
  // [JDK8u RISC-V Port Backport Note]
  // Upstream removed CMS and BiasedLocking in JDK 14/15, and transitioned
  // to BarrierSetAssembler in JDK 11.
  // In JDK 8u, we preserve the classic card table write barrier and biased locking
  // mechanisms, aligning with AArch64 JDK 8u architecture while using RV64GC AMO/CAS.
  ```
通过这一方法论，确保最终产物在代码结构上是纯粹正统的“JDK 1.8 端口”，同时汇编质量达到现代 RISC-V 架构的最优水平！

---

### 4.24 G1 写屏障（SATB Pre-Barrier / Card-Marking Post-Barrier）中的 `t0` 寄存器别名冲突与 16 字节对齐栈保护

#### 1. 问题现象
在开启 `-XX:+UseG1GC` 运行解释器时，执行 `putfield` / `putstatic` / `aastore` 字节码会触发莫名其妙的 `NullPointerException`、对象引用损坏（bad oop），或在慢速路径调用 `SharedRuntime::g1_wb_pre` / `g1_wb_post` 时违反 RISC-V psABI 16 字节栈对齐约束。

#### 2. 根因深度剖析
1. **`t0` 内部临时寄存器与调用者入参 `tmp` 发生别名冲突**：
   - 在 [`TemplateTable::do_oop_store`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp#L138-L155) 中，最初调用 `g1_write_barrier_pre` 时传入了 `t0`（`x5`）作为 `tmp` 参数：
     ```cpp
     __ g1_write_barrier_pre(obj, noreg /* pre_val */, xthread, t0 /* tmp */, true, false);
     ```
   - 而在 `MacroAssembler::g1_write_barrier_pre` 和 `g1_write_barrier_post` 内部，不仅使用 `t0` 读取线程局部 SATB 队列激活状态（`JavaThread::satb_mark_queue_offset()`），而且所有的远偏移 `ld/sd` 与 `movptr` 宏指令默认也会隐式借用 `t0` 作为基底计算寄存器！一旦 `tmp == t0` 或 `pre_val == t0`，刚加载出来的 `pre_val` 或 `queue_index` 立即被下一条宏指令覆写！
2. **慢速路径 `push_reg(RegSet, sp)` 的 C 栈对齐陷阱**：
   - 当 SATB 缓冲区或 Dirty Card 队列满时，屏障慢路径通过 `call_VM_leaf` 调用 C++ 函数 `SharedRuntime::g1_wb_pre` / `g1_wb_post`。在进入 C 函数前需要将存活寄存器压入物理 C 栈 `sp`。
   - 如果压入的寄存器个数为奇数（例如 `g1_write_barrier_post` 仅需保存 `store_addr` 1 个寄存器，或 `g1_write_barrier_pre` 保存 `obj`、`pre_val`、`x10` 共 3 个寄存器），朴素的 `sp -= count * 8` 会破坏 RISC-V psABI 要求的 **16 字节栈对齐**（`sp % 16 == 0`）。

#### 3. 完美解决方案
1. 在 [`MacroAssembler::g1_write_barrier_pre`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L788-L858) 与 [`g1_write_barrier_post`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L860-L932) 入口处增加严格的 `assert_different_registers(..., t0)` 断言，并在 [`TemplateTable::do_oop_store`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp#L138-L155) 中改传 `t1` 作为 `tmp`，传入 `x13` 作为 `tmp2`；
2. 实现 [`MacroAssembler::push_reg(unsigned int bitset, Register stack)`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L325-L361) 与 `pop_reg`：当 `bitset` 中寄存器数量 `count` 为奇数时，自动预留一个额外的 `wordSize`（`offset = is_even(count) ? 0 : wordSize`），始终保证 `sp` 按 16 字节对齐。

---

### 4.25 `MacroAssembler::load_byte_map_base` 中的 `BarrierSet` 继承体系断言陷阱（`kind()` vs `is_a()`）

#### 1. 问题现象
在 `-XX:+UseG1GC` 下启动 JVM 时，立即触发断言崩溃：
```text
# Internal Error (macroAssembler_riscv.cpp:570), pid=7, tid=19
# assert(bs->kind() == BarrierSet::CardTableModRef || bs->kind() == BarrierSet::CardTableExtension) failed: wrong barrier set kind
```

#### 2. 根因深度剖析
在 JDK 8u 的 `BarrierSet` 类层次结构中：
```
BarrierSet
  └── ModRefBarrierSet
        └── CardTableModRefBS          (kind() == BarrierSet::CardTableModRef)
              ├── CardTableExtension   (kind() == BarrierSet::CardTableExtension, ParallelGC 使用)
              └── G1SATBCardTableModRefBS
                    └── G1SATBCardTableLoggingModRefBS (kind() == BarrierSet::G1SATBCTLogging, G1GC 使用)
```
- SerialGC 与 CMS 使用的 `CardTableModRefBS` 其 `kind()` 为 `BarrierSet::CardTableModRef`；
- ParallelGC 使用的 `CardTableExtension` 其 `kind()` 为 `BarrierSet::CardTableExtension`；
- 而 **G1GC** 使用的 `G1SATBCardTableLoggingModRefBS` 虽然继承自 `CardTableModRefBS`（拥有完全合法的 `byte_map_base` 卡表基址），但它的 `bs->kind()` 返回的是 `BarrierSet::G1SATBCTLogging`！
- 如果在 `load_byte_map_base` 中使用 `bs->kind() == ...` 枚举值硬比较，G1GC 必然触发断言失败。

#### 3. 完美解决方案
对齐 AArch64 端口（[`macroAssembler_aarch64.cpp:3496`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/macroAssembler_aarch64.cpp#L3496)），将 [`MacroAssembler::load_byte_map_base`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L570) 中的断言修正为多态类型检查：
```cpp
assert(bs->is_a(BarrierSet::CardTableModRef), "Resizeable card table is not supported");
```
`is_a(BarrierSet::CardTableModRef)` 对 `CardTableModRefBS` 的所有子类（包括 Serial、Parallel、CMS、G1）均返回 `true`。

---

### 4.26 引用数组拷贝桩（`generate_oop_copy`）的写屏障缺失与目的地址/计数寄存器破坏

#### 1. 问题现象
在实现 [`StubGenerator::generate_oop_copy`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp#L620-L695)（用于 `System.arraycopy` 拷贝引用数组 `Object[]`）时，若缺失 `gen_write_ref_array_pre_barrier` / `gen_write_ref_array_post_barrier`，或在拷贝循环中直接递增了目标指针 `d`（`c_rarg1`）并递减了长度 `count`（`c_rarg2`），会导致 G1GC / CMS 在并发标记或年轻代回收时漏标跨代引用或 SATB 原值！

#### 2. 根因深度剖析
引用数组批量拷贝需要遵循严格的“三段式”契约：
1. **拷贝前（Pre-Barrier）**：调用 `gen_write_ref_array_pre_barrier(d, count, dest_uninitialized)`。对于 G1GC（且 `!dest_uninitialized`），在覆写目标区间 `[d, d + count * heapOopSize)` 之前，调用 `BarrierSet::static_write_ref_array_pre(d, count)` 将目标数组将被覆写的旧引用记录到 SATB 缓冲区；
2. **核心拷贝循环**：支持正向与反向（处理重叠区间 `s < d < s + count`）复制；
3. **拷贝后（Post-Barrier）**：调用 `gen_write_ref_array_post_barrier(d, count, tmp)`，将修改过的目标区间对应的卡表项（Card Table）批量标脏（对于 G1GC 调用 `BarrierSet::static_write_ref_array_post(d, count)`，对于其他分代 GC 在汇编中直接计算 `[start >> 9, end >> 9]` 循环写入 `0`）。
- **致命陷阱**：如果在第 2 步的核心拷贝循环中，直接修改了寄存器 `d`（`c_rarg1`）和 `count`（`c_rarg2`），当循环结束进入第 3 步 `gen_write_ref_array_post_barrier(d, count, t0)` 时，`count` 已经变成 `0`！后置写屏障直接退化为空操作（No-op）！

#### 3. 完美解决方案
在 [`StubGenerator::generate_oop_copy`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp#L635-L692) 中：
- 先在入口调用 `gen_write_ref_array_pre_barrier(d, cnt, dest_uninitialized)`；
- 在拷贝循环中使用临时工作寄存器 `t0`（拷贝 `s`）、`t1`（拷贝 `d`）、`x13`（拷贝 `cnt`）执行循环迭代，**完整保留 `d`（`c_rarg1`）和 `cnt`（`c_rarg2`）的原始起始地址与元素个数**；
- 循环结束后，将完好无损的 `(d, cnt)` 传给 `gen_write_ref_array_post_barrier(d, cnt, t0)`。

---

### 4.27 JNI Native 方法出口（`generate_native_entry`）误调 `remove_activation` 导致的 `synchronized native` 双重解锁崩溃

#### 1. 问题现象
当执行任何 `synchronized native` 方法（或 JNI 返回后检查异常/Safepoint）时，虚拟机抛出莫名其妙的 `java.lang.IllegalMonitorStateException`！

#### 2. 根因深度剖析
在 [`InterpreterGenerator::generate_native_entry`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp#L830-L888) 中，Native 方法的退出路径与普通 Java 字节码方法（`generate_normal_entry`）有着本质区别：
1. 在 Native 方法从 C 函数返回并切回 `_thread_in_Java` 状态后：
   - 第 852~868 行已经显式包含了一段针对 `if (synchronized)` 的解锁逻辑：
     ```cpp
     if (synchronized) {
       ...
       __ unlock_object(c_rarg1);
     }
     ```
2. 然而，在调用完 `result_handler` 之后（第 882 行），最初的桩代码错误地调用了：
   ```cpp
   __ remove_activation(vtos);
   ```
   而 `InterpreterMacroAssembler::remove_activation` 内部会检查当前 `Method::access_flags()` 是否包含 `JVM_ACC_SYNCHRONIZED`；一旦发现是同步方法，它会再次去读取第一个 `BasicObjectLock` 槽位。此时该槽位的 `obj` 字段已经在第 867 行的 `unlock_object` 中被清零（`sd(zr, obj_offset)`），于是 `remove_activation` 判定“锁被提前非法释放”，立刻调用 `InterpreterRuntime::throw_illegal_monitor_state_exception`！
3. 此外，在 Native 调用刚返回、检查 `pending_exception` 时，由于 C 函数调用早已破坏了 caller-saved 的 `xmethod`（`x31/t6`），且 `generate_fixed_frame(true)` 将 native 帧的 `xbcp` 初始化为 `0`（`NULL`），若直接调用 `InterpreterMacroAssembler::call_VM`（其内部会执行 `save_bcp()` 并将 `last_Java_sp` 设为未更新的 `esp`），会在栈回溯时引发 `bcp` 非法指针崩溃。

#### 3. 完美解决方案
严格对齐 AArch64 端口（[`templateInterpreter_aarch64.cpp:1140-1215`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/templateInterpreter_aarch64.cpp#L1140-L1215)）：
1. 在 Native 状态转换（`_thread_in_native_trans` -> `_thread_in_Java`）慢路径中，除检查 `SafepointSynchronize::_state` 外，一并检查 `JavaThread::suspend_flags_offset() != 0`，并使用手工对齐栈帧 + `jalr` 调用 `JavaThread::check_special_condition_for_native_trans`；
2. 在检查 `pending_exception` 前，先通过 `__ get_method(xmethod)` 恢复 `xmethod`，并将 `xbcp` 恢复为 `ConstMethod::codes_offset()`（合法的 `bci == 0`），且使用 `__ MacroAssembler::call_VM`（以 `sp` 而非 `esp` 作为 `last_Java_sp`）抛出异常；
3. 在 `result_handler` 执行完毕后，**严禁调用 `remove_activation(vtos)`**，而是直接执行退栈指令序列：
   ```cpp
   __ ld(esp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
   __ leave();
   __ andi(sp, esp, -16);
   __ ret();
   ```

---

### 4.28 栈帧回溯（`frame::sender_for_compiled_frame`）与 Runtime Stub `OopMap` 的“四重连环坑”

#### 1. 问题现象
当程序触发全局 Safepoint（如 `System.gc()` 或偏向锁撤销 `VM_RevokeBias`），Java 线程在解释器或 Runtime Stub（如 `SafepointBlob`）中阻塞，VMThread 调用 `JavaThread::oops_do` 遍历线程栈时，触发致命段错误（SIGSEGV）或断言失败：
```text
# Internal Error (frame.cpp:400), pid=7, tid=19
# assert(cb == _cb) failed: must be
```
或者在 `OopMapSet::oops_do` 中读取到非法的 `OopMap` 偏移而崩溃！

#### 2. 根因深度剖析（四重连环 Bug）
这是整个 Runtime 基座中最隐蔽、涉及多组件协同的四重底层陷阱：
1. **陷阱一：`frame_riscv.cpp` 照抄 AArch64 `sender_sp_offset` 导致读取错误的 `saved_fp`**：
   - 在 AArch64 中，`fp` 指向栈帧底部的 `saved_fp`（即 `link_offset == 0`，`return_addr_offset == 1`，`sender_sp_offset == 2`）。因此在 AArch64 中，`sender_sp - frame::sender_sp_offset` 恰好等于 `sender_sp - 2`（即 `saved_fp` 的地址）。
   - 但在 **RISC-V** 中，遵循 GCC/Clang 标准栈帧约定，`fp` 指向调用者的栈顶（即 **`sender_sp_offset == 0`**，`return_addr_offset == -1`，**`link_offset == -2`**）！
   - 此前 [`frame_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp) 的 `safe_for_sender`（第 118 行）和 `sender_for_compiled_frame`（第 308 行）直接从 AArch64 抄写了：
     ```cpp
     intptr_t** saved_fp_addr = (intptr_t**) (sender_sp - frame::sender_sp_offset); // Bug! 在 RISC-V 上等于 sender_sp - 0!
     ```
     导致从 `SafepointBlob`（其走的是 `sender_for_compiled_frame`！）向上回溯到调用者解释器帧时，读出的 `saved_fp` 不是 `[sender_sp - 2]` 处的旧 `fp`，而是 `[sender_sp + 0]` 处的栈顶垃圾数据！
2. **陷阱二：`RegisterSaver::save_live_registers` 中 `OopMap` 栈槽单位混淆（`wordSize` vs `BytesPerInt`）**：
   - 在 HotSpot 中，无论 32 位还是 64 位架构，`OopMap` 构造函数的 `frame_size` 参数以及 `VMRegImpl::stack2reg(idx)` 的偏移单位，**永远是 4 字节的编译器栈槽（`VMRegImpl::stack_slot_size == 4 == BytesPerInt`）**，而不是 8 字节机器字（`wordSize`）！
   - 此前 [`sharedRuntime_riscv.cpp:74`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L74) 将 `OopMap` 大小写成了 `frame_words`（少乘了 2），且将寄存器在栈上的偏移写成了 `VMRegImpl::stack2reg(i)`（应为 `VMRegImpl::stack2reg(2 * (i - 1))`），导致 `CodeBlob` 的 `_frame_size` 只有实际栈帧的一半，且 `OopMap` 记录的栈偏移全部错位！
3. **陷阱三：`RegisterSaver` 将固定寄存器 `xthread` / `xheapbase` 与自身跳板 `t0` 记入 `OopMap`**：
   - `xthread`（`x23`）和 `xheapbase`（`x27`）是全局固定寄存器，`t0`（`x5`）是跳转到 `SafepointBlob` 时被覆盖的跳板寄存器，它们绝不能作为可重定位/可被 GC 扫描的普通寄存器写入 `OopMap`。
4. **陷阱四：`generate_handler_blob` 调用 `set_last_Java_frame` 时传入 `NULL` 作为 `last_Java_pc`**：
   - 在 [`SharedRuntime::generate_handler_blob`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L343-L358) 中，生成完 `call_VM_leaf` 后，通过 `oop_maps->add_gc_map(__ offset() - start, map)` 将 `OopMap` 绑定在 `call_VM_leaf` 返回点 `retaddr` 的 PC 偏移上。
   - 但调用前却写成了 `__ set_last_Java_frame(sp, noreg, NULL, t0)`（传入了 `NULL` PC！）。当 VMThread 在 Safepoint 扫描该线程栈时，`frame::pc()` 找不到匹配的 `OopMap` PC 偏移，立即在 `OopMapSet::find_map_at_offset` 中触发致命断言！

#### 3. 完美解决方案
1. 在 [`frame_riscv.cpp:118`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp#L118) 与 [`frame_riscv.cpp:308`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp#L308) 中，将 `saved_fp_addr` 修正为 `sender_sp + frame::link_offset`（即 `sender_sp - 2`），将返回地址修正为 `sender_sp + frame::return_addr_offset`（即 `sender_sp - 1`）；
2. 在 [`sharedRuntime_riscv.cpp:72-108`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L72-L108) 中，将 `OopMap` 槽位数修正为 `frame_words * wordSize / BytesPerInt`，将第 `i` 个通用寄存器的栈槽偏移修正为 `VMRegImpl::stack2reg(2 * (i - 1))`，并跳过 `xthread`、`xheapbase`、`t0`；
3. 在 [`sharedRuntime_riscv.cpp:343-358`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L343-L358)（以及 `generate_deopt_blob`、`generate_uncommon_trap_blob`）中，绑定 `Label retaddr` 并传入 `__ set_last_Java_frame(sp, noreg, retaddr, t0)` 与 `__ call_VM_leaf_base(..., &retaddr)`，使 `last_Java_pc` 与 `add_gc_map(retaddr.offset() - start, map)` 精确对齐到同一个指令字节！

---

### 4.29 偏向锁（Biased Locking）延迟激活（4 秒）掩盖的 `ObjectSynchronizer::fast_exit` 致命断言崩溃

#### 1. 问题现象
短时间运行的程序（`< 4 秒`）在默认参数下通过了 `synchronized` 测试，但一旦运行超过 4 秒或加上 `-XX:BiasedLockingStartupDelay=0`，在退出第一个 `synchronized` 方法或 `monitorexit` 字节码时立即触发致命断言崩溃：
```text
# Internal Error (/workspace/hotspot/src/share/vm/runtime/synchronizer.cpp:187), pid=7, tid=2
# assert(!object->mark()->has_bias_pattern()) failed: should not see bias pattern here
# V [libjvm.so+0x7d360c] ObjectSynchronizer::fast_exit(oopDesc*, BasicLock*, Thread*)+0x6e
# V [libjvm.so+0x7d3962] ObjectSynchronizer::slow_exit(oopDesc*, BasicLock*, Thread*)+0x30
# V [libjvm.so+0x53d608] InterpreterRuntime::monitorexit(JavaThread*, BasicObjectLock*)+0x96
```

#### 2. 根因深度剖析
这是“三位一体”移植范式中 **JDK 8 与 JDK 19 架构差异**最典型、最隐蔽的经典案例：
1. **为什么短程序不崩、长程序或 `-XX:BiasedLockingStartupDelay=0` 必崩？**
   - 在 JDK 8u 中，`UseBiasedLocking` 默认是 `true`，但有一个默认 4000ms 的启动延迟（`BiasedLockingStartupDelay = 4000`）。在 JVM 启动的前 4 秒内，分配的对象其 Mark Word 是无锁状态（`0b001`），不会启用偏向模式。
   - 一旦超过 4 秒（或显式设置 `-XX:BiasedLockingStartupDelay=0`），新分配对象的 Prototype Header 开启偏向模式（低 3 位为 `0b101`，`has_bias_pattern() == true`）。
2. **为什么 `InterpreterMacroAssembler::unlock_object` 不能直接调用 `InterpreterRuntime::monitorexit`？**
   - 当调用 `InterpreterRuntime::monitorenter` 时，C++ 层的 `ObjectSynchronizer::fast_enter` 会检查 `UseBiasedLocking` 并调用 `BiasedLocking::revoke_and_rebias`，将对象头偏向当前线程（Mark Word 低 3 位保持 `0b101`）。
   - 然而，在解锁路径上，HotSpot 的设计契约规定：**偏向锁的解锁是一个纯汇编层的空操作（No-op，仅需检查对象头低 3 位是否为 `0b101` 并清空 `BasicObjectLock::_obj`），绝不允许进入 C++ 慢路径 `InterpreterRuntime::monitorexit` -> `ObjectSynchronizer::slow_exit` -> `ObjectSynchronizer::fast_exit`！**
   - 因为 `ObjectSynchronizer::fast_exit`（[`synchronizer.cpp:187`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/runtime/synchronizer.cpp#L187)）的第一行就是：
     ```cpp
     assert(!object->mark()->has_bias_pattern(), "should not see bias pattern here");
     ```
3. **为什么只看 JDK 19 RISC-V 代码会掉坑？**
   - 在 JDK 15（JEP 374）之后，偏向锁被废弃并最终从 JDK 19 中彻底移除，因此 `/Users/wujiahua/code/jdk19` 的 `macroAssembler_riscv.cpp` 和 `interp_masm_riscv.cpp` 中完全没有 `biased_locking_enter` / `biased_locking_exit`！
   - 更进一步，JDK 19 将 `InterpreterRuntime::monitorexit` 改为了 `JRT_LEAF`（用 `call_VM_leaf` 调用且直接传 `BasicObjectLock*` 到 `c_rarg0`），而 JDK 8u 中 `InterpreterRuntime::monitorexit` 是 `IRT_ENTRY_NO_ASYNC`（第一个参数必须是 `JavaThread*`，必须用 `call_VM` 调用）！

#### 3. 完美解决方案
遵循“三位一体”移植范式，结合 JDK 8u AArch64 的偏向锁状态机（[`macroAssembler_aarch64.cpp:402-576`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/macroAssembler_aarch64.cpp#L402-L576)、[`interp_masm_aarch64.cpp:644-794`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/interp_masm_aarch64.cpp#L644-L794)）与 JDK 19 RISC-V 的原子 `lr.d/sc.d` 指令（[`macroAssembler_riscv.cpp:1245-1370`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L1245-L1370)、[`interp_masm_riscv.cpp:356-468`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp#L356-L468)）：
1. 在 `MacroAssembler` 中基于 `lr.d.aqrl` + `sc.d.rl` 实现 `cmpxchgptr` 与 `cmpxchg_obj_header`；
2. 完整实现 `MacroAssembler::biased_locking_enter`（支持匿名偏向 CAS、跨 Epoch 重偏向 CAS、Prototype 撤销 CAS 回退）与 `MacroAssembler::biased_locking_exit`（检测 `mark & 7 == 5` 直接跳转 `done`）；
3. 在 `InterpreterMacroAssembler::lock_object` 与 `unlock_object` 中集成偏向锁快路径、轻量级栈锁 CAS 快路径、同页栈指针递归锁检测（`((mark - sp) & (7 - os::vm_page_size())) == 0`）以及 `call_VM` 慢路径回退。

---

### 4.30 定长可修补序列 `movptr`（6 条指令 24 字节）vs 变长立即数加载 `li`（1~6 条指令）的指令覆写踩踏

#### 1. 问题现象與潜在灾难
在解释器阶段，常量与指针加载几乎不需要在运行期被回填修改。但一旦进入 Step 3.0（代码修补、重定位与内联缓存层）与 C1/C2 编译器阶段，大量代码点在编译期发射的是 **`0`（`NULL`）或占位符**，并在运行期通过 `NativeMovConstReg::set_data`、`Relocation::pd_set_data_value` 或 `NativeMovRegMem::set_offset` 动态回填真实的 48 位地址或偏移！
典型场景包括：
1. `CompiledStaticCall::emit_to_interp_stub` 中发射 `mov_metadata(xmethod, (Metadata*)NULL)`，在运行期 `set_to_interpreted` 时回填真实的 `Method*`；
2. `MacroAssembler::ic_call` 中发射 `movptr(xmethod, Universe::non_oop_word())`，在单态内联缓存命中时回填 `Klass*` 或 `CompiledICHolder*`；
3. C1 `PatchingStub`（`jobject2reg_with_patching`、`klass2reg_with_patching`、字段偏移动态修补 `NativeMovRegMem`）在编译期由于类尚未加载，先发射 `movptr(reg, 0)`，待运行期触发类初始化后再原位将 48 位 `Klass*` / `oop` / 字段偏移写入指令流。

#### 2. 根因深度剖析
在早期解释器开发中，[`MacroAssembler::movptr`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L63-L88) 对特殊值做了贪婪的变长压缩优化：
- 当 `val == 0` 时，收缩为 **1 条指令（4 字节）**：`mv(dst, zr)`（即 `addi dst, zr, 0`）；
- 当 `is_simm12(val)` 时，收缩为 **1 条指令（4 字节）**：`addi(dst, zr, val)`；
- 当 `is_simm32(val)` 时，收缩为 **1~2 条指令（4~8 字节）**：`lui + [addiw]`；
- 甚至在 48 位展开末尾，当低 6 位 `offset == 0`（例如 8 字节对齐的指针且第 6~11 位恰好 aligned）时，省去了最后一条 `addi(dst, dst, offset)`，变成了 **5 条指令（20 字节）**！

然而，在 RISC-V 64（Sv48，48 位虚拟地址空间）上，由于指令定长 32 位且立即数位宽受限（U-type 20 位，I-type 12 位），在不借用额外临时寄存器的前提下，加载任意 48 位地址的标准可修补序列 `movptr` 必须严格由 **6 条固定指令（24 字节）** 组成：
```text
  +0: lui   rd, imm[47:29] + sign_fixup   // 加载高 19 位（含符号位补偿）
  +4: addi  rd, rd, imm[28:17]            // 拼接中间 12 位（至此完成高 31 位）
  +8: slli  rd, rd, 11                    // 左移 11 位
 +12: addi  rd, rd, imm[16:6]             // 拼接接下来的 11 位（无符号，0..0x7ff）
 +16: slli  rd, rd, 6                     // 左移 6 位
 +20: addi  rd, rd, imm[5:0]              // 拼接最低 6 位（或合入 jalr/ld 的偏移字段）
```
如果编译期 `movptr(reg, 0)` 只发射了 1 条 4 字节指令，而运行期 `patch_addr_in_movptr` 按照 24 字节（6 条指令）去回填，就会**直接覆写后续 5 条正常指令（20 字节）**，或者在 `NativeInstruction::is_movptr_at` 校验时直接触发 `fatal("not a movptr...")` 崩溃！

#### 3. 完美解决方案
严格区分两个宏汇编原语的语义边界（[`macroAssembler_riscv.cpp:36-110`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L36-L110)）：
1. **`MacroAssembler::movptr(Register dst, intptr_t val)`**：**严格定长 6 条指令（24 字节）**。内部调用 `movptr_with_offset(dst, (address)val, offset)` 后**无条件**发射第 6 条 `addi(dst, dst, offset)`（哪怕 `val == 0` 或 `offset == 0`），确保任何 `movptr` 调用点都满足 `NativeInstruction::is_movptr_at` 契约；
2. **`MacroAssembler::li(Register Rd, int64_t imm)`**：**变长优化立即数加载（1~6+ 条指令）**。仅用于编译期已知且运行期绝不修补的纯算术常数加载；
3. **`Assembler::li32(Register Rd, int32_t imm)`**：**严格定长 2 条指令（8 字节：`lui + addiw`）**。专用于开启压缩指针（`UseCompressedOops` / `UseCompressedClassPointers`）时 32 位 `narrowOop` / `narrowKlass` 的可修补加载（`set_narrow_oop` / `set_narrow_klass`），由 `NativeInstruction::is_li32_at` 与 `patch_imm_in_li32` 识别并原位回填。

---

### 4.31 `movoop` 与 `mov_metadata` 漏掉 `Relocation` 注册及 JDK 8 vs JDK 19 的 `jobject` 立即数鸿沟

#### 1. 问题现象与架构鸿沟
在审查 [`MacroAssembler::mov_metadata`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L2122-L2131) 与 [`MacroAssembler::movoop`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L2133-L2147) 时，发现了两个致命问题：
1. **重定位丢失 Bug**：早期代码虽然调用 `oop_recorder()->find_index(obj)` 构造了 `RelocationHolder rspec = oop_Relocation::spec(oop_index)`，但随后直接调用了 `movptr(dst, (intptr_t)obj)`，**完全漏掉了 `code_section()->relocate(pc(), rspec)`**！
2. **JDK 8 vs JDK 19 架构鸿沟（NMethod Entry Barrier 与常量池 vs 内联立即数）**：
   - 在 JDK 19 RISC-V（`macroAssembler_riscv.cpp:1244-1278`）中，`movoop(Register dst, jobject obj, bool immediate)` 的默认参数是 `immediate = false`，且内部耦合了 JDK 12+ 引入的 `BarrierSet::barrier_set()->barrier_set_nmethod()`（NMethod Entry Barrier，用于并发 GC 栈扫描）。当 `immediate == false` 时，JDK 19 通过 `ld_constant` 生成基于常量池（Constant Pool Section）的 `auipc + ld` 指令。
   - 但在 **JDK 8u** 中，既没有 `BarrierSetNMethod`，且 **C1 编译器（`LIR_Assembler`）在编译时不分配常量池节（Constant Pool Section）**！JDK 8u 的 C1 要求 `movoop` 默认将 `oop` 作为内联立即数（6 条指令 `movptr`）直接编码进指令流，并在 GC 移动对象时通过 `oop_Relocation` 原位修补这 6 条指令！

#### 2. 根因深度剖析：如果漏掉 `code_section()->relocate(pc(), rspec)` 会发生什么？
在 HotSpot JIT 编译器（C1/C2）中，编译线程运行在编译器上下文中，不能直接裸持堆内 `oopDesc*`，因此所有常量对象都是用 **`jobject`（即指向 `JNIHandleBlock` 槽位的间接指针 `oop*`）** 表示的！
- 编译期：`movoop(dst, obj)` 发射到指令流里的初始值实际上是 **`(intptr_t)obj`（即 `JNIHandle` 的地址，而不是堆内对象的真实地址 `JNIHandles::resolve(obj)`！）**；
- 安装期（`nmethod::new_nmethod` $\rightarrow$ `CodeBuffer::copy_code_to` $\rightarrow$ `fix_oop_relocation`）：HotSpot 遍历代码段中的所有 `oop_Relocation`，将 `OopRecorder` 中的 `jobject` 解引用（`JNIHandles::resolve`）得到真正的堆内 `oopDesc*`，然后调用 `Relocation::pd_set_data_value` 把指令流中的 `JNIHandle` 地址改写为真正的 `oopDesc*`！
- 运行期（GC）：每次 Young GC / Full GC 移动了该对象，GC 都会遍历 `nmethod` 的 `oop_Relocation` 并调用 `pd_set_data_value` 更新指令流中的立即数地址。
- **后果**：如果 `movoop` 漏掉了 `code_section()->relocate(pc(), rspec)`，生成出来的机器码里装进寄存器的永远是 `JNIHandle` 槽位自身的C堆地址！一旦 Java 代码访问该对象的字段或方法表，就会把 `JNIHandle` 槽位当成 `oopDesc` 头去解引用 `_metadata._klass`，瞬间触发诡异的堆损坏或 `SIGSEGV`！

#### 3. 完美解决方案
在 [`macroAssembler_riscv.cpp:104-110, 2122-2180`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L2122-L2180) 中：
1. 实现 `MacroAssembler::mv(Register Rd, Address dest)`：在 `InstructionMark` 保护下显式调用 `code_section()->relocate(pc(), dest.rspec())`，再发射定长 `movptr(Rd, (intptr_t)dest.target())`；
2. 将 `MacroAssembler::movoop(Register dst, jobject obj, bool immediate = true)` 的默认参数设为 `immediate = true`（对齐 JDK 8u C1/C2 规范），内部通过 `mv(dst, Address((address)obj, rspec))` 同时完成重定位注册与 6 指令定长发射；
3. 在 `set_narrow_oop` 中使用 JDK 8u 的 `oopDesc::encode_heap_oop(JNIHandles::resolve(obj))`（替代 JDK 19 的 `CompressedOops::narrow_oop_value`），在 `set_narrow_klass` 中使用 `Klass::encode_klass(k)`（替代 JDK 19 的 `CompressedKlassPointers::encode`），并均在 `InstructionMark` 下注册 `rspec` 后发射 2 指令定长 `li32`。

---

### 4.32 RISC-V `jal` $\pm 1\text{MB}$ 跳转范围限制、多线程原子修补与 `NativeCallTrampolineStub` 桥接机制

#### 1. 架构挑战：为什么 RISC-V 的 `NativeCall` 不能直接用 `auipc + jalr`（8 字节）？
在 RISC-V 指令集中：
- 单条无条件跳转并链接指令 `jal ra, offset`（J-type）具有 20 位有符号半字偏移，寻址范围仅为 **$\pm 1\text{MB}$**；
- 而双指令组合 `auipc t0, hi20; jalr ra, t0, lo12` 虽然能覆盖 $\pm 2\text{GB}$ 的整个 `CodeCache`，但它占用了 **2 条 32 位指令（8 字节）**！

在 HotSpot 中，编译代码中的方法调用点（Static Call、Opt Virtual Call、Virtual IC Call）会在多线程并发执行（不进入全局 Safepoint！）的情况下，由其他线程通过 `NativeCall::set_destination_mt_safe` 动态修改跳转目标（例如从 `resolve_static_call_stub` 切换到已编译目标方法入口或 `to_interp_stub`）。
- 如果 `NativeCall` 由 2 条指令（`auipc + jalr`）组成，当线程 A 刚修改完第一条 `auipc`、尚未修改第二条 `jalr` 时，线程 B 恰好执行完旧的 `auipc` 并接着执行新的 `jalr`（**Tearing / 撕裂读**），会拼接出一个完全错误的随机目标地址并跳飞崩溃！
- 在 AArch64 中，单条 `bl` 指令具有 26 位字偏移（$\pm 128\text{MB}$）；而在 RISC-V 中，为了保证调用点本身是**单条 32 位对齐指令（`NativeCall::instruction_size == 4`）**从而具备天然的 32 位写原子性，当 `ReservedCodeCacheSize > 1MB`（即 `MacroAssembler::far_branches() == true`）时，**几乎所有跨方法的编译调用都可能超出 `jal` 的 $\pm 1\text{MB}$ 范围**！

#### 2. 完美解决方案：`NativeCall`（4B `jal`）+ `NativeCallTrampolineStub`（20B）双层原子修补架构
我们在 [`macroAssembler_riscv.cpp:477-538`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L477-L538) 与 [`nativeInst_riscv.cpp:173-290`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp#L173-L290) 中完整构建了 Trampoline 桥接体系：
1. **编译期（`MacroAssembler::trampoline_call` & `emit_trampoline_stub`）**：
   - 当 `far_branches()` 为真时，先在当前编译方法的 **Stub Section**（紧跟在方法主代码区之后，距离主代码区远小于 $1\text{MB}$！）分配一个 20 字节的 `NativeCallTrampolineStub`，并注册 `trampoline_stub_Relocation::spec(call_pc)` 将该 Stub 与主代码区的调用点绑定：
     ```text
     Stub Section (8 字节对齐, 20 字节):
       +0: auipc t0, 0
       +4: ld    t0, 12(t0)     // PC 相对加载 +12 处的 64 位目标地址
       +8: jr    t0             // 跳转到 t0 (jalr zr, t0, 0)
      +12: .dword <64-bit dest> // 8 字节对齐的 64 位目标地址槽位
     ```
   - 随后在主代码区的调用点发射单条 `jal(ra, 0)`（自跳占位符），并注册调用重定位（`runtime_call_type` / `static_call_type` / `virtual_call_type` 等）。
2. **代码搬移与安装期（`CodeBuffer::relocate_code_to` $\rightarrow$ `Relocation::pd_set_call_destination`）**：
   - 当 `CodeBuffer` 扩容或拷贝生成最终 `nmethod` 时，若目标地址 `dest` 距离调用点在 $\pm 1\text{MB}$ 以内，直接将调用点的 `jal` 修补为跳向 `dest`；
   - 若超出 $\pm 1\text{MB}$，通过 `trampoline_stub_Relocation::get_trampoline_for` 找到配对的 `NativeCallTrampolineStub`，将 64 位 `dest` 写入 Stub 的 `+12` 数据槽位，并将调用点的 `jal` 修补为跳向本方法尾部的 `trampoline_stub`！
3. **运行期并发修补（`NativeCall::set_destination_mt_safe`）**：
   - 先原位原子更新 `NativeCallTrampolineStub` `+12` 处的 64 位目标地址并执行 `OrderAccess::release()`；
   - 再根据新目标 `dest` 是否在 $\pm 1\text{MB}$ 内，用单次 32 位写操作将调用点的 `jal` 指令原子更新为直跳 `dest` 或跳向 `trampoline_stub_addr`，最后刷新 I-Cache。整个过程绝不会出现多指令撕裂！

---

### 4.33 JDK 8 vs JDK 19 在 `CompiledIC` / `CompiledStaticCall` 与 `NativeJump::patch_verified_entry` 上的四处接口鸿沟

#### 1. 现象与编译/运行期隐患
在将 JDK 19 的 `compiledIC_riscv.cpp`、`icBuffer_riscv.cpp` 与 `nativeInst_riscv.cpp` 迁回 JDK 8u 时，暴露出四处关键的跨版本接口与信号处理鸿沟：
1. **鸿沟一：`CompiledDirectStaticCall` vs `CompiledStaticCall` 及 `cleanup_call_site` 的归属转移**：
   - JDK 19 将静态调用拆分为独立的 `CompiledDirectStaticCall` 类，并将 `CompiledIC::cleanup_call_site` 与 `CompiledIC::is_icholder_call_site` 移入了平台无关的共享文件 `compiledIC.cpp`。
   - 而在 **JDK 8u** 中，类名仍为 `CompiledStaticCall`（继承自 `NativeCall`），没有 `to_trampoline_stub_size()` 成员函数；且 [`compiledIC.hpp:197-198`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/code/compiledIC.hpp#L197-L198) 声明的 `CompiledIC::cleanup_call_site` 与 `is_icholder_call_site` **必须在各 CPU 后端的 `compiledIC_<arch>.cpp` 中提供定义**！此外，[`compiledIC.hpp:326`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/code/compiledIC.hpp#L326) 中原本仅对 `#if defined(AARCH64)` 开放了带 `address mark` 参数的 `emit_to_interp_stub(CodeBuffer &cbuf, address mark)` 重载，需将 `defined(RISCV)` 一并纳入。
2. **鸿沟二：`NativeFenceI::instruction_size` 与 `ifence()` 发射字节数必须严格相等**：
   - 在 `MacroAssembler::emit_static_call_stub()` 中，第一条指令是 `ifence()`，紧接着是 `mov_metadata(xmethod, NULL)`。而在 `CompiledStaticCall::set_to_interpreted` 中，定位 `method_holder` 的方式是硬编码偏移：`nativeMovConstReg_at(stub + NativeFenceI::instruction_size)`！
   - 在 JDK 19 中，`ifence()` 被展开为 4 条指令（`1 + 3` 条，共 16 字节），因此 `NativeFenceI::instruction_size` 为 `16`；而在我们的实现中，`ifence()` 直接发射单条 `fence.i`（4 字节）。若误将 `NativeFenceI::instruction_size` 设为 `16`，`set_to_interpreted` 会直接跳过 `mov_metadata` 的前 3 条指令，指到第 4 条指令中间触发 `fatal("not a movptr...")`！因此必须将 `NativeFenceI::instruction_size` 严格定为 `1 * NativeInstruction::instruction_size`（4 字节）。
3. **鸿沟三：`MacroAssembler::la_patchable` 对 `CodeCache` 外部 C++ 运行时函数地址的兼容性**：
   - 在 JDK 19 的 `la_patchable` 中，只要 `rtype != relocInfo::none`，就断言 `assert(CodeCache::contains(dest.target()))` 并强制使用 32 位 PC 相对偏移（`auipc + jalr`）。
   - 但在 JDK 8u 的解释器与 Stub 中，部分 `far_call` / `far_jump` 会直接传入位于 `libjvm.so` 共享库中的 C++ 函数地址（不在 `CodeCache` 内，且在 64 位地址空间或 QEMU 下与 `CodeCache` 的距离可能超过 $\pm 2\text{GB}$！）。因此我们在 [`macroAssembler_riscv.cpp:441-455`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L441-L455) 中增加了 `CodeCache::contains(dest.target()) && is_offset_in_range(dest.target() - pc(), 32)` 双重判定：在 `CodeCache` 内使用 2 指令 `auipc + jalr`（配合 `InlineCacheBuffer` 与 `NativeJump`），在 `CodeCache` 外自动平滑回退到 6 指令全地址 `movptr_with_offset + jalr`！
4. **鸿沟四：`NativeJump::patch_verified_entry` 的非法指令码与 `os_linux_riscv.cpp` 信号处理器的契约对齐**：
   - 当某个已编译方法失效（`make_not_entrant_or_zombie`）时，`NativeJump::patch_verified_entry` 会将方法入口 `verified_entry` 的第一条 4 字节指令原子替换为跳向 `SharedRuntime::get_handle_wrong_method_stub()` 的指令。
   - 若距离超出 `jal` 的 $\pm 1\text{MB}$ 范围，无法用单条 4 字节跳转指令覆盖，此时必须写入一条 4 字节陷阱指令，交由 Linux 信号处理器拦截并重定向 PC 到 `get_handle_wrong_method_stub()`。
   - 我们的 [`os_linux_riscv.cpp:293-297`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp#L293-L297) 拦截的是 `sig == SIGILL && nativeInstruction_at(pc)->is_sigill_zombie_not_entrant()`（对应编码 `0xffffffff`，即 `NativeIllegalInstruction::instruction_code`）。因此 `patch_verified_entry` 必须写入 `0xffffffff`（触发 `SIGILL`），而绝不能写入 `ebreak`（触发 `SIGTRAP`），从而与信号处理器形成 100% 闭环。

---

### 4.34 C1 寄存器分配池（`c1_FrameMap_riscv.cpp`）中保留寄存器与 `pd_last_allocatable_cpu_reg` 边界陷阱

#### 1. 问题现象与架构挑战
在实现 C1 编译器的 [`c1_FrameMap_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.hpp) 与 [`c1_FrameMap_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.cpp) 时，线性扫描寄存器分配器（`LinearScan`）依赖 `FrameMap::_cpu_rnr2reg` 与 `pd_last_allocatable_cpu_reg` 决定哪些物理寄存器可以被分配给虚拟寄存器（LIR_Opr），哪些必须严格隔离。如果映射顺序或边界常数偏差 1 个槽位，编译出的机器码将直接把普通 Java 局部变量分配到 `xthread`（`x23`）、`xheapbase`（`x27`）、`sp`（`x2`）、`fp`（`x8`）或宏汇编内部跳板 `t0`（`x5`）/ `t1`（`x6`）上，瞬间摧毁 VM 运行时状态！

#### 2. 根因深度剖析
RISC-V 64 拥有 32 个通用整数寄存器（`x0` ~ `x31`），但在 HotSpot C1 编译器中，有 **10 个整数寄存器** 具有不可侵犯的专用职责，绝不能交给 `LinearScan` 自由分配：
1. `zr`（`x0`）：硬件零寄存器；
2. `ra`（`x1`）：返回地址寄存器（跨调用与方法序言/尾声使用）；
3. `sp`（`x2`）：物理 C/Java 栈指针；
4. `gp`（`x3`）与 `tp`（`x4`）：Linux RISC-V psABI 全局指针与平台线程指针（禁止触碰）；
5. `t0`（`x5`）与 `t1`（`x6`）：HotSpot `MacroAssembler` 与 `LIR_Assembler` 的内部临时跳板寄存器（`rscratch1` / `rscratch2`），几乎任何一条宏指令都可能隐式改写它们；
6. `fp`（`x8` / `s0`）：栈帧指针；
7. `xthread`（`x23` / `s7`）：当前 `JavaThread*` 指针；
8. `xheapbase`（`x27` / `s11`）：压缩指针（Compressed Oops）堆基址寄存器。

而 C1 的 `LinearScan` 判断一个 CPU 寄存器号 `rnr`（非物理编码，而是 `map_register(rnr, reg)` 注册的逻辑编号 `0..31`）是否可分配的唯一依据是：
```cpp
rnr <= pd_last_allocatable_cpu_reg
```
这意味着：**所有 22 个可分配寄存器必须集中映射在逻辑编号 `rnr = 0 .. 21`（即 `pd_last_allocatable_cpu_reg = 21`），而上述 10 个保留寄存器必须严格映射在 `rnr = 22 .. 31`！**

此外，在 JDK 8u 的 `LIR_OprDesc` 契约中：
- `FrameMap::as_oop_opr(Register r)` 生成的是 `single_cpu` 引用寄存器（因为即便在 64 位平台上，HotSpot C1 的对象引用 `T_OBJECT` / `T_ARRAY` 在 `LIR_Opr` 层面也表示为单槽位 CPU 寄存器 `single_cpu`，只有 `T_LONG` 和 `T_ADDRESS` 的 `as_long_opr` / `as_pointer_opr` 才表示为双槽位 `double_cpu`：`cpu_reg2rnr(r)` 与本身配对！）。

#### 3. 完美解决方案
在 [`c1_FrameMap_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.hpp#L48-L62) 与 [`c1_FrameMap_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.cpp#L110-L185) 中：
1. 将 `x10~x17`（`a0~a7`）、`x28~x31`（`t3~t6`）、`x7`（`t2`）、`x9`（`s1`）、`x18~x22`（`s2~s6`）、`x24~x26`（`s8~s10`）共 **22 个通用寄存器** 依次映射至 `rnr = 0..21`，并将 `pd_last_allocatable_cpu_reg` 设为 `21`；
2. 将 `xheapbase(x27)`、`xthread(x23)`、`fp(x8)`、`zr(x0)`、`ra(x1)`、`sp(x2)`、`gp(x3)`、`tp(x4)`、`t0(x5)`、`t1(x6)` 依次映射至不可分配区间 `rnr = 22..31`。

---

### 4.35 C1 `DEOPTIMIZE_WHEN_PATCHING` 机制差异与未解析类/字段的跨版本修补鸿沟

#### 1. 问题现象与架构鸿沟
当 C1 编译一个方法时，如果方法内引用的某个类尚未初始化、或者某个字段的偏移量尚未解析（Unresolved Field / Klass / Mirror / Appendix），C1 会生成一个 `PatchingStub` 跳入 `Runtime1::access_field_patching_id` 或 `load_klass_patching_id`。
在对比 `/Users/wujiahua/code/jdk19/src/hotspot/cpu/riscv/` 与本仓 JDK 8u 时，发现了一个极其关键的跨版本架构差异：
1. **JDK 19 的 `DEOPTIMIZE_WHEN_PATCHING` 共享宏**：
   - 在 JDK 19 中，`globalDefinitions_riscv.hpp` 定义了 `#define DEOPTIMIZE_WHEN_PATCHING`，且共享代码 `c1_Runtime1.cpp` 中使用 `#ifdef DEOPTIMIZE_WHEN_PATCHING` 统一将修补动作转为直接触发调用方栈帧逆优化（`deoptimize`），从而省去了在运行期跨核自修改多条机器指令的复杂同步。
2. **JDK 8u 的 `#ifndef TARGET_ARCH_aarch64` 硬编码分支**：
   - 而在 **JDK 8u** 的 [`c1_Runtime1.hpp:132-139`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.hpp#L132-L139) 与 [`c1_Runtime1.cpp:1066-1277`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.cpp#L1066-L1277) 中，**根本不存在 `DEOPTIMIZE_WHEN_PATCHING` 宏**！
   - JDK 8u 的 `c1_Runtime1.cpp` 默认所有非 `TARGET_ARCH_aarch64` 架构都走 x86 风格的 `Runtime1::patch_code`：它会把 `PatchingStub` 中备份的待修补机器码按字节拷贝覆盖主代码区的 `nop` 序列，并调用 `NativeGeneralJump::replace_mt_safe`！
   - 在 RISC-V（RVWMO 弱内存模型 + 无跨核硬件 I-Cache 自动同步）上，一个 `movptr` 长达 6 条指令（24 字节），如果在多线程并发执行且未进入 Safepoint 的情况下原地按字节拷贝修改 24 字节指令流，其他正在执行该代码段的 CPU 核心极易读到半新半旧的指令流而崩溃！

#### 2. 完美解决方案
严格遵循“三位一体”范式，对齐本仓 JDK 8u AArch64 的设计（[`c1_Runtime1.hpp:132-143`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.hpp#L132-L143)、[`c1_Runtime1.cpp:1280`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.cpp#L1280)）：
1. 在 [`c1_Runtime1.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.hpp) 中声明 `static int patch_code_riscv(JavaThread* thread, StubID stub_id);`，并在 [`c1_Runtime1.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.cpp) 中将 `TARGET_ARCH_riscv` 加入 `#if !defined(TARGET_ARCH_aarch64) && !defined(TARGET_ARCH_riscv)` 屏蔽宏；
2. 在 [`c1_Runtime1_riscv.cpp:801-922`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp#L801-L922) 中实现 `Runtime1::patch_code_riscv` 与 `generate_patching`：当首次命中最慢路径时，先在 C++ 层完成类加载/字段解析/类初始化，随即将当前 `nmethod` 标记为 `make_not_entrant()` 并调用 `Deoptimization::deoptimize_frame(thread, caller_frame.id())` 回退到解释器执行，下次编译时直接生成无桩的全速代码！

---

### 4.36 C1 `StubFrame` 栈帧布局、`fp` 基准偏移与 `save_live_registers` 奇偶对齐槽位的三方契约

#### 1. 问题现象与架构鸿沟
在实现 [`c1_Runtime1_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp) 时，如果照搬 AArch64 的 `StubFrame::load_argument` 偏移计算或忽略 `push_reg` 的 16 字节对齐填充槽位，会导致：
- `new_type_array` / `new_object_array` / `new_multi_array` 从栈上加载的参数错位 16 字节（读出 `ra` 和 `old_fp` 当作 `klass` 或 `length`）；
- `handle_exception_from_callee` 和 `generate_patching` 覆写错误的栈槽，导致异常捕获或逆优化返回时跳飞；
- `OopMap` 记录的寄存器保存位置与物理栈帧偏离 1 个机器字（8 字节），GC 扫描 `Runtime1` Stub 栈帧时全部读错寄存器！

#### 2. 根因深度剖析
1. **RISC-V `fp`（栈帧顶部）vs AArch64 `rfp`（栈帧底部）的 16 字节基准差**：
   - 在 AArch64 中，`enter()` 执行 `stp rfp, lr, [sp, #-16]!; mov rfp, sp`，此时 `rfp` 指向当前栈帧**底部**（`[rfp + 0] = old_fp`，`[rfp + 8] = lr`，`[rfp + 16]` 才是调用者的 `caller_sp`）。因此 AArch64 的 `StubFrame::load_argument` 访问 `[rfp + (offset_in_words + 2) * 8]`，且异常返回地址保存在 `[rfp + 8]`（`rfp, wordSize`）。
   - 而在本仓 **RISC-V** 中，[`MacroAssembler::enter()`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp#L384-L390)（即 `build_frame(16)`）令 **`fp` 指向栈帧顶部（即调用者的 `caller_sp`）**，而 `ra` 保存在 **`[fp - 8]`**（`frame::return_addr_offset == -1`），`old_fp` 保存在 **`[fp - 16]`**（`frame::link_offset == -2`）！
   - 因此，在 RISC-V 的 `StubFrame` 中，调用者压栈的参数位于 **`[fp + offset_in_words * 8]`**（通过 `load_parameter(offset_in_words, reg)` 访问），而 `generate_handle_exception` 和 `generate_patching` 读取/修补返回地址时，必须访问 **`Address(fp, frame::return_addr_offset * BytesPerWord)`（即 `[fp - 8]`）**！
2. **`save_live_registers` 中 27 个整数寄存器的奇数对齐槽位与 `restore_live_registers_except_r10` 的精妙对偶**：
   - 在 [`c1_Runtime1_riscv.cpp:178-236`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp#L178-L236) 中，`save_live_registers` 通过 `push_reg(RegSet::range(x5, x31), sp)` 保存 `x5..x31` 共 **27 个整数寄存器（奇数）**。
   - 根据 `MacroAssembler::push_reg` 的 16 字节对齐规则，当寄存器数量为奇数时，会在 `sp` 最低地址处（即紧贴随后压栈的 32 个浮点寄存器上方）**自动预留 1 个 8 字节对齐槽位（2 个 32-bit VMReg 槽位）**，使整数寄存器区共占用 $27 + 1 = 28$ 个机器字！加上 32 个浮点寄存器（32 words）和 `enter()` 的 `ra + fp`（2 words），总栈帧大小严格为 **`reg_save_frame_size = 62` words**。
   - 因此，在 `Runtime1::initialize_pd()` 计算 `OopMap` 偏移表时，遍历完 `f0..f31`（占用 `sp_offset = 0..64`）后，**必须先执行 `sp_offset += 2` 跳过这 1 个对齐字**，再从 `sp_offset = 66` 开始依次映射 `x5..x31`（`66..118`）！
   - 更精妙的是，在 `restore_live_registers_except_r10`（保持返回值寄存器 `x10` 不被恢复覆盖）中，连续调用两次 `pop_reg`：
     - 第一次：`pop_reg(RegSet::range(x5, x9), sp)` —— 弹出 **5 个寄存器（奇数）**，`pop_reg` 自动跳过 1 个 word 对齐槽位，共令 `sp += 6 * 8`；
     - 第二次：`pop_reg(RegSet::range(x11, x31), sp)` —— 弹出 **21 个寄存器（奇数）**，`pop_reg` 在起始偏移处再次跳过 1 个 word，而这个被跳过的 word **恰好就是 `x10` 在栈上的保存槽位**！两次 `pop_reg` 总计弹出 $6 + 22 = 28$ words，与 `push_reg(x5..x31)` 的 28 words 严丝合缝！

---

### 4.37 JDK 8 vs JDK 19 在 `CodeStubs`（`RangeCheckStub` / `DeoptimizeStub` / `G1*BarrierStub`）上的四处契约差异

#### 1. 问题现象与架构鸿沟
在将 JDK 19 的 `c1_CodeStubs_riscv.cpp` 移植到 JDK 8u（[`c1_CodeStubs_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_CodeStubs_riscv.cpp)）时，存在四处因 JEP 演进而产生的签名与行为鸿沟：
1. **鸿沟一：`RangeCheckStub` 在 JDK 8u 中是双参数而非三参数**：
   - JDK 19 的 `RangeCheckStub(CodeEmitInfo* info, LIR_Opr index, LIR_Opr array)` 接收 `array` 操作数以生成包含数组长度的详细异常信息（`throw_range_check_failed(index, array)`）；
   - 而 JDK 8u 的 [`c1_CodeStubs.hpp:208`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_CodeStubs.hpp#L208) 构造函数是 `RangeCheckStub(CodeEmitInfo* info, LIR_Opr index, bool throw_index_out_of_bounds_exception)`，根本没有 `_array` 成员！其慢路径 `Runtime1::range_check_failed_id` 只接收 `t0` 中的 `index` 单个参数（调用 `generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_range_check_failed), true)`）。
2. **鸿沟二：`DeoptimizeStub` 在 JDK 8u 中不带 `trap_request` 参数**：
   - JDK 19 的 `DeoptimizeStub` 将 `_trap_request` 装入 `t0` 传给 `Runtime1::deoptimize(JavaThread* thread, jint trap_request)`；
   - 而 JDK 8u 的 [`Runtime1::deoptimize(JavaThread* thread)`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.cpp#L533) 是单参数函数，`DeoptimizeStub::emit_code` 直接 `far_call(Runtime1::entry_for(Runtime1::deoptimize_id))` 即可。
3. **鸿沟三：`CounterOverflowStub::_method` 在 JDK 8u 中可能是寄存器或元数据常量**：
   - JDK 8u 的 `LIRGenerator` 通过 `LIR_Opr` 传递 `_method`，因此在 [`CounterOverflowStub::emit_code`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_CodeStubs_riscv.cpp#L67-L82) 中需要同时兼容 `_method->is_single_cpu()`（直接 `ce->store_parameter(_method->as_register(), 0)`）与 `_method->is_constant()`（通过 `mov_metadata(t0, m)` 后压栈）两种形态。
4. **鸿沟四：G1 C1 Pre/Post Barrier Stub 归属位置（JEP 304 之前）**：
   - JDK 19 将 `G1PreBarrierStub` / `G1PostBarrierStub` 迁入了 `gc/g1/c1/g1BarrierSetC1.cpp`；
   - 而 JDK 8u 尚未引入 `BarrierSetC1`，`G1PreBarrierStub::emit_code` 与 `G1PostBarrierStub::emit_code` 必须在 [`c1_CodeStubs_riscv.cpp:259-341`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_CodeStubs_riscv.cpp#L259-L341) 中实现，对应的慢速队列刷新桩 `g1_pre_barrier_slow_id` 与 `g1_post_barrier_slow_id` 则在 [`c1_Runtime1_riscv.cpp:668-785`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp#L668-L785) 中生成。

---

### 4.38 C++ 基类构造期虚表（`vptr`）尚未就绪导致 `NativeCall::get_trampoline()` 中 `code->is_nmethod()` 恒为 `false` 的自跳死循环陷阱

#### 1. 问题现象
在完成 Step 3.2（`c1_LIRGenerator_riscv.cpp` 与 `c1_LIRAssembler_riscv.cpp`）并开启 `-XX:+TieredCompilation -XX:TieredStopAtLevel=1` 后，C1 编译器成功将 `java.lang.String::hashCode` 编译为第一个 `nmethod`，但当解释器通过 I2C 适配器首次进入该编译方法并触发内部的方法调用时，JVM 线程瞬间陷入 **100% CPU 死循环**！
通过 GDB 附加观察到：C1 编译生成的 `trampoline_call` 机器码竟然停留在 **`0x000000ef`（`jal ra, 0`，即原地自跳死循环）**，根本没有重定向到尾部的 `NativeCallTrampolineStub`，也没有修补为真实目标地址！

#### 2. 根因深度剖析：C++ 对象构造顺序与 `CodeBlob` / `nmethod` 虚函数表切换时机
1. 在 RISC-V 上，由于 `jal` 单指令跳转范围仅有 $\pm 1\text{MB}$，`MacroAssembler::trampoline_call` 先在调用点发射一条占位指令 `jal ra, pc+0`（偏移为 0），并在 Stub 区分配一个 `NativeCallTrampolineStub`（记录 `trampoline_stub_Relocation`）。
2. 当 C1 完成代码发射、调用 `nmethod::new_nmethod` 安装方法时，构造链为：
   ```cpp
   nmethod::nmethod(...)
     -> CodeBlob::CodeBlob(name, cb, ...)
       -> cb->copy_code_to(this)
         -> CodeBuffer::relocate_code_to(CodeBlob* dest)
           -> CallRelocation::fix_relocation_after_move(src, dest)
             -> pd_set_call_destination(addr)
               -> NativeCall::set_destination_mt_safe(dest)
                 -> NativeCall::get_trampoline()
   ```
3. 此前在 [`nativeInst_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp#L244-L259) 的 `NativeCall::get_trampoline()` 中，写了如下防御判断：
   ```cpp
   CodeBlob *code = CodeCache::find_blob_unsafe(call_addr);
   if (code == NULL || !code->is_nmethod()) {
     return NULL;
   }
   ```
4. **致命陷阱**：当 `cb->copy_code_to(this)` 被调用时，**当前执行点正处于基类构造函数 `CodeBlob::CodeBlob` 的函数体内部，派生类 `nmethod::nmethod` 的构造函数体尚未开始执行**！
   根据 C++ 标准（ISO C++98 §12.7），在基类 `CodeBlob` 构造期间，对象 `this` 的动态类型就是 `CodeBlob`，其虚函数表指针（`vptr`）指向的是 `CodeBlob` 的虚表，此时调用虚函数 `code->is_nmethod()` **永远返回 `false`**！
5. 结果导致：在 `copy_code_to` 重定位阶段，当 `dest` 距离调用点超过 $\pm 1\text{MB}$（例如调用 `SharedRuntime::get_ic_miss_stub()` 或远距离方法）时，`set_destination_mt_safe` 试图通过 `get_trampoline()` 查找蹦床桩，却因 `!code->is_nmethod()` 提前返回了 `NULL`！结果既没有把 `dest` 写入蹦床桩，也没有把 `jal` 指向蹦床桩，调用点保留了初始发射时的 `jal ra, 0` 自跳指令！

#### 3. 完美解决方案
对照本仓 JDK 8u AArch64 的 [`nativeInst_aarch64.cpp:126-135`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/nativeInst_aarch64.cpp#L126-L135) 与 JDK 19 RISC-V 的 `nativeInst_riscv.cpp`，在 [`nativeInst_riscv.cpp:244-258`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp#L244-L258) 中直接使用 `CodeCache::find_blob_unsafe(call_addr)` 并调用 `trampoline_stub_Relocation::get_trampoline_for(call_addr, (nmethod*)code)`（因为 `get_trampoline_for` 仅访问基类 `CodeBlob` 已初始化完毕的 `relocation_begin()` / `relocation_end()` 非虚成员），绝不在 `CodeBlob` 构造期依赖虚函数 `is_nmethod()`！

---

### 4.39 `NativeFenceI` 指令序列长度失配与 JDK 8u vs JDK 19 C1 `build_frame` 栈帧大小契约鸿沟

#### 1. 鸿沟一：`Assembler::ifence()` 在 `UseConservativeFence` 下的 8 字节发射 vs `NativeFenceI::instruction_size` 4 字节硬编码
1. **问题现象**：
   当 C1 编译代码首次执行静态方法调用（`invokestatic`）并进入 `SharedRuntime::resolve_static_call_C` 修补调用点时，触发断言失败：
   ```
   Internal Error (nativeInst_riscv.cpp:154), Error: not a movptr
   ```
   调用栈显示 `CompiledStaticCall::verify()` 在检查静态调用桩（`emit_static_call_stub`）时，认为首条指令不是 `movptr`（或 `is_fencei_at` 偏移失配）！
2. **根因剖析**：
   - 在 [`assembler_riscv.hpp:2331-2339`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp#L2331-L2339) 中，`Assembler::ifence()` 的实现为：
     ```cpp
     void ifence() {
       if (UseConservativeFence) {
         fence(ir, ir);
       }
       fence_i();
     }
     ```
     由于默认 `UseConservativeFence = true`，`__ ifence()` 实际发射了 **2 条 32 位指令（`fence ir, ir` + `fence.i`，共 8 字节）**！
   - 然而，在 [`nativeInst_riscv.hpp:425-431`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp#L425-L431) 中，`NativeFenceI::instruction_size` 此前被硬编码为枚举常量 `4`（单条指令）！
   - 当 [`compiledIC_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/compiledIC_riscv.cpp#L91-L166) 中的 `CompiledStaticCall::set_to_interpreted` 跨过開头的 `NativeFenceI` 去定位紧随其后的 `mov_metadata`（`movptr`）和 `NativeJump` 时，使用了 `stub + NativeFenceI::instruction_size`（仅跳过 4 字节），结果落在了第二条指令 `fence.i` 上，导致 `nativeMovConstReg_at` 立即报 `not a movptr`！
3. **此外，`NativeJump::jump_destination()` 的未绑定占位值 `-1` 解码**：
   - `emit_static_call_stub` 在生成静态调用桩尾部发射的是跳向自身的占位跳转 `jr(t0)`（目标地址初始化为 `0`）。而 HotSpot 共享层 `CompiledStaticCall::verify()` 要求未绑定的 `NativeJump::jump_destination()` 返回 **`(address)-1`**。因此 [`NativeJump::jump_destination()`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp#L375-L386) 在读取到目标地址为 `0` 时必须将其归一化返回为 `(address)-1`。
4. **完美解决方案**：
   在 [`nativeInst_riscv.hpp:425-431`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp#L425-L431) 中将 `NativeFenceI::instruction_size()` 定义为动态计算函数 `(1 + (UseConservativeFence ? 1 : 0)) * NativeInstruction::instruction_size`，并同步更新 `is_fencei_at(address)` 与 `compiledIC_riscv.cpp`。

#### 2. 鸿沟二：JDK 8u vs JDK 19 在 `C1_MacroAssembler::build_frame` / `remove_frame` 上的 `2 * wordSize`（16 字节）契约差
1. **问题现象**：
   开启 C1 编译后，一旦触发栈回溯（如异常抛出或 GC 扫描 C1 编译栈帧），`frame::sender_for_compiled_frame` 计算出的调用者 `sender_sp` 与真实 `sp` 相差整整 **16 字节（2 个机器字）**！
2. **根因剖析**：
   - 在 **JDK 19** 中，`FrameMap::framesize()` 在共享层已经把保存 `fp` 和 `ra` 的 2 个机器字（`2 * wordSize = 16` 字节）算进了 `frame_size_in_bytes` 中，因此 JDK 19 的 `C1_MacroAssembler::build_frame(int frame_size_in_bytes, ...)` 直接调用 `MacroAssembler::build_frame(frame_size_in_bytes)`。
   - 但在 **JDK 8u** 中（对照 [`c1_LIRAssembler_aarch64.cpp:176-184`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/c1_LIRAssembler_aarch64.cpp#L176-L184) 与 [`c1_MacroAssembler_aarch64.cpp:365-377`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/c1_MacroAssembler_aarch64.cpp#L365-L377)），`LIR_Assembler::initial_frame_size_in_bytes()` **显式减去了 `2 * wordSize`**（即 `in_bytes(frame_map()->framesize_in_bytes()) - 2 * wordSize`），因为 JDK 8u 的契约规定：`initial_frame_size_in_bytes()` 返回的是**不含 `fp/ra` 的净帧大小**，由 `C1_MacroAssembler::build_frame` 和 `remove_frame` 自己加上 `2 * wordSize`！
3. **完美解决方案**：
   在 [`c1_MacroAssembler_riscv.cpp:317-332`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_MacroAssembler_riscv.cpp#L317-L332) 的 `C1_MacroAssembler::build_frame` 与 `remove_frame` 中，调用底层 `MacroAssembler::build_frame(frame_size_in_bytes + 2 * wordSize)` 与 `MacroAssembler::remove_frame(frame_size_in_bytes + 2 * wordSize)`，使物理分配的栈帧大小与 `CodeBlob::frame_size()`（`initial_frame_size_in_bytes() + 2 * wordSize`）100% 吻合！

---

### 4.40 `DeoptimizationBlob` 临时 `CodeBuffer` 地址被 `I2C/C2I adapters` 复用引发的逆优化栈帧误判，以及 `SignatureHandlerGenerator` 栈上代码生成的寻址陷阱

#### 1. 问题现象
在 `-Xcomp -XX:+TieredCompilation -XX:TieredStopAtLevel=1` 压力模式下，当 C1 编译的 `ComprehensiveTest::<clinit>` 因类尚未初始化触发 `patch_code_riscv` 并进入 `SharedRuntime::generate_deopt_blob` 执行逆优化解包（`Deoptimization::unpack_frames`）时，JVM 触发致命断言崩溃：
```
Internal Error (frame_riscv.cpp:544), pid=38, tid=1075827728
assert(is_interpreted_frame()) failed: interpreted frame expected
```
通过 GDB 打印 `Deoptimization::unpack_frames` 中的栈回溯发现：
- `stub_frame`（即 `thread->last_frame()`，本应是 `DeoptimizationBlob` 自身的栈帧）的 `_cb` 竟然被识别成了 **`BufferBlob: I2C/C2I adapters`**！
- 更诡异的是，`stub_frame.pc()` 的值 `0x400b4bb3cc` 落在了 `I2C/C2I adapters` 内部，而真正在执行的 `DeoptimizationBlob` 代码明明位于 `0x400b4fb0a0`！导致 `stub_frame.sender()` 调用了 `sender_for_interpreter_frame` 而不是 `sender_for_compiled_frame`，直接崩溃！

#### 2. 根因深度剖析：临时 `BufferBlob` 的生命周期、`CodeCache` 空闲块复用与绝对寻址 `movptr` vs PC 相对寻址 `la`
1. 在 [`sharedRuntime_riscv.cpp:2598-2601`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L2598-L2601) 的 `SharedRuntime::generate_deopt_blob()` 中：
   ```cpp
   CodeBuffer buffer("deopt_blob", 2048, 1024);
   ...
   address the_pc = __ pc();
   __ set_last_Java_frame(sp, fp, the_pc, t0);
   ```
2. 请注意 `CodeBuffer buffer("deopt_blob", 2048, 1024)` 的工作机制：
   - 它首先在 `CodeCache` 中分配一个临时的 `BufferBlob`（假设起始地址为 `0x400b4bb000`），所有指令先发射到这个临时缓冲区中，此时 `the_pc` 的值是临时缓冲区内的地址 `0x400b4bb3cc`！
   - 当 `generate_deopt_blob` 末尾调用 `DeoptimizationBlob::create(&buffer, ...)` 时，HotSpot 会在 `CodeCache` 中分配一个新的正式 `DeoptimizationBlob`（地址为 `0x400b4fb0a0`），调用 `buffer.copy_code_to(blob)` 将机器码复制过去，**然后立即调用 `BufferBlob::free` 将刚才那个临时缓冲区（`0x400b4bb000`）释放回 `CodeCache` 空闲链表**！
   - 紧接着，JVM 初始化继续向下走，`AdapterHandlerLibrary` 请求分配 `BufferBlob: I2C/C2I adapters`，`CodeCache` **把刚刚释放的 `0x400b4bb000` 这块内存原封不动地分配给了 `I2C/C2I adapters`**！
3. 那么为什么 `DeoptimizationBlob` 在 `0x400b4fb3cc` 执行 `__ set_last_Java_frame(sp, fp, the_pc, t0)` 时，会把旧的临时地址 `0x400b4bb3cc` 写进 `JavaThread::_anchor._last_Java_pc` 呢？
   - 因为此前在 [`macroAssembler_riscv.cpp:702-710`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L702-L710) 中，`set_last_Java_frame` 调用了 `movptr(temp, last_java_pc)`（或者调用了内部退化为 `movptr` 的 `la(Register, const address)`）！
   - `movptr` 发射的是 **6 条加载 48 位绝对常量的指令（`lui + addi + slli...`）**，并且**没有重定位项（Relocation）**！因此当 `buffer.copy_code_to(blob)` 将代码从 `0x400b4bb000` 拷贝到 `0x400b4fb0a0` 时，`movptr` 中的立即数纹丝不动，依然硬编码着临时缓冲区的旧地址 `0x400b4bb3cc`！
4. **硬币的另一面（对偶陷阱）：`SignatureHandlerGenerator` 在 C 栈上生成代码后直接 `memcpy` 到 `CodeCache`**：
   - 当我们将 `MacroAssembler::la(Register Rd, const address dest)` 修复为：当 `dest` 与当前 `pc()` 的距离在 $\pm 2\text{GB}$ 以内时，发射 PC 相对的 **`auipc + addi`** 指令对（因此当 `DeoptimizationBlob` 整体从临时 `CodeBuffer` 平移拷贝到正式 `DeoptimizationBlob` 时，`the_pc` 与当前指令的相对偏移保持不变，自动指向新位置！）后，必须警惕一个**方向相反的特例**：
   - 在 [`interpreterRT_riscv.cpp:146`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interpreterRT_riscv.cpp#L146) 的 `SignatureHandlerGenerator::generate` 中：
     ```cpp
     void InterpreterRuntime::SignatureHandlerGenerator::generate(uint64_t fingerprint) {
       ...
       __ la(x10, Interpreter::result_handler(method()->result_type()));
     }
     ```
   - 为什么这里用 PC 相对的 `la(Register, const address)` 会崩溃？因为 [`AdapterHandlerLibrary::create_native_wrapper`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/runtime/sharedRuntime.cpp#L2431-L2440) 在调用 `SignatureHandlerGenerator` 时，传入的 `CodeBuffer` **根本不在 `CodeCache` 里，而是 C++ 线程栈上的一个局部数组 `unsigned char buffer[1024]`（地址如 `0x4002c70eb0`）**！生成完后，它甚至不调 `copy_code_to`，而是直接用裸 `memcpy(handler, buffer, ...)` 把机器码拷进 `CodeCache`（地址如 `0x400b53f000`）！
   - 在 C 栈地址 `0x4002c70eb0` 与 `CodeCache` 地址 `0x400b53f000` 之间，差值约 `136MB`（小于 2GB），如果 `SignatureHandlerGenerator` 发射了无重定位的 `auipc + addi`，经裸 `memcpy` 搬移 136MB 后，PC 相对偏移就会指飞 136MB！

#### 3. 完美解决方案
1. 在 [`macroAssembler_riscv.cpp:702-710, 1480-1495`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L702-L710) 中：
   - `set_last_Java_frame` 使用 `la(temp, last_java_pc)`；
   - `MacroAssembler::la(Register Rd, const address dest)` 在 32 位有符号偏移范围内使用 `auipc + addi`（满足同一 `CodeBlob` 内部如 `the_pc` 的位置无关自引用）；
2. 在 [`interpreterRT_riscv.cpp:146`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interpreterRT_riscv.cpp#L146) 中，将外部地址显式包装为 `ExternalAddress`：
   ```cpp
   __ la(x10, ExternalAddress(Interpreter::result_handler(method()->result_type())));
   ```
   因为 `la(Register, const Address&)` 针对 `external_word_type` 发射的是定长绝对地址加载 `movptr`（不依赖生成时的 `pc()`），即便在 C 栈上生成再裸 `memcpy` 到 `CodeCache` 也 100% 准确！

---

### 4.41 AArch64（48 位无符号 `movz/movk`）与 RISC-V（47 位无符号 / `-1` 有符号 `lui/addi`）在 `Universe::non_oop_word()` 上的第 48 位借位翻转鸿沟

#### 1. 问题现象
在 `-Xcomp -XX:+TieredCompilation -XX:TieredStopAtLevel=1` 下跑通全部 5 次逆优化后，程序推进到 `ComprehensiveTest` 的 `[Test 6] System.gc()`。在 GC Safepoint 扫描 `CodeCache` 中已编译方法的内联缓存（`CompiledIC`）时，触发致命断言失败：
```
Internal Error (compiledIC.cpp:144), pid=38, tid=1093567792
assert(!is_clean || is_optimized() || cached_value() == NULL) failed: Sanity check
```
通过 GDB 检查该 `CompiledIC`：
- 该调用点是一个尚未解析的虚调用（Clean IC，目标指向 `SharedRuntime::get_ic_miss_stub()`）；
- 其关联的 `NativeMovConstReg`（由 `MacroAssembler::ic_call` 生成，初值为 `Universe::non_oop_word()`）读出的 `data()` 竟然是 **`0xffffefffffffffff`**，而 `Universe::non_oop_word()` 的值却是 **`0x0000ffffffffffff`**！
- 因为 `0xffffefffffffffff != 0x0000ffffffffffff`，`CompiledIC::cached_value()` 误以为该 Clean IC 缓存了一个非空的 `Metadata*`，直接触发断言爆炸！

#### 2. 根因深度剖析：为什么 `0x0000ffffffffffff` 经过 RISC-V `movptr` 后会变成 `0xffffefffffffffff`？
这是一处极其隐蔽且深刻的 **AArch64 vs RISC-V 64 位立即数编码架构鸿沟**：
1. **`Universe::non_oop_word()` 的来源**：
   - 在 [`universe.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/memory/universe.cpp) 中，`Universe::non_oop_word()` 返回 `(oop)os::non_memory_address_word()`。
   - 在 AArch64（[`os_linux_aarch64.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_aarch64/vm/os_linux_aarch64.cpp)）中，`os::non_memory_address_word()` 返回 `(char*)0xffffffffffff`（**48 位全 1，高 16 位为 0**）。
   - 为什么 AArch64 选 `0x0000ffffffffffff`？因为 AArch64 的 `movptr` 由无符号的 `movz xN, #imm16; movk xN, #imm16, lsl #16; movk xN, #imm16, lsl #32`（3 条 16 位无符号拼接指令）组成，它能精确表示任意 **48 位无符号整数 `[0, 0x0000ffffffffffff]`**，且高 16 位恒清零！
2. **RISC-V `movptr` 的有符号扩展本质**：
   - 相反，RISC-V 没有无符号的 `movz/movk` 指令！RISC-V 的 6 指令 `movptr` 序列是：
     ```riscv
     lui   Rd, imm[47:29]       # 1. 加载高 20 位到 Rd[31:12]，并将 Rd[31]（即原地址的 bit 48）符号扩展到 Rd[63:32]！
     addi  Rd, Rd, imm[28:17]   # 2. 加上有符号 12 位立即数
     slli  Rd, Rd, 11           # 3. 左移 11 位
     addi  Rd, Rd, imm[16:6]    # 4. 加上 11 位立即数
     slli  Rd, Rd, 6            # 5. 左移 6 位（累计左移 17 位）
     addi  Rd, Rd, imm[5:0]     # 6. 加上低 6 位立即数
     ```
   - 请仔细推演当输入 `imm = 0x0000ffffffffffff`（低 48 位全 1，bit 47 为 1）时会发生什么：
     - 在 `movptr_with_offset` 中，首先截断取低 48 位：`imm64 = 0x0000ffffffffffff`；
     - `imm[28:17]` 全为 1（即 `-1`，负数）。为了补偿 `addi Rd, Rd, -1` 产生的向高位的借位，算法会令 `upper += (lower >> 11)`（当 `lower < 0` 时其实应当 `+1` 补偿借位，但即便看 `lui` 的符号位）：
     - 在 `0x0000ffffffffffff` 中，第 47 位是 1，左移 16 位再算术右移 33 位后，`lui` 的最高位 `Rd[31]` 为 1，**在 RV64 中 `lui` 会把 `Rd[31]` 符号扩展到 `Rd[63:32]` 全 1**；而 `addi` 的 `-1` 导致第 31 位向第 32 位借位变成 `0`！再经过 `slli 11` 和 `slli 6`（共左移 17 位），原本的第 31 位变成了最终的 **第 48 位（bit 48 = 0）**，而更高的位 `[63:49]` 全是符号扩展出的 `1`！
     - 结果：无论是在 CPU 寄存器里执行这 6 条指令，还是由 `NativeMovConstReg::data()`（`MacroAssembler::get_target_of_movptr`）反解这 6 条指令，得到的结果都是 **`0xffffefffffffffff`**！
   - 换言之，RISC-V 的 6 指令 `movptr` 只能无损编码 **47 位无符号用户态地址 `[0, 0x00007fffffffffff]`**（bit 47 为 0）或者 **64 位全 1 的 `-1`（`0xffffffffffffffff`）**！

#### 3. 完美解决方案
严格对齐 JDK 19 RISC-V 上游设计（`/Users/wujiahua/code/jdk19/src/hotspot/os_cpu/linux_riscv/os_linux_riscv.cpp:86` 与 `macroAssembler_riscv.cpp:1182-1266`）：
1. 在 [`os_linux_riscv.cpp:104-112`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp#L104-L112) 中，将 `os::non_memory_address_word()` 改为返回 **`(char*)-1`（即 `0xffffffffffffffff`）**；
2. 在 [`macroAssembler_riscv.cpp:57-125, 936-995`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp#L57-L125) 的 `movptr_with_offset`、`patch_addr_in_movptr` 与 `get_target_of_movptr` 中：
   - 修正 `upper` 对 `lower` 负数借位的补偿为标准的 `if (lower < 0) { upper += (1 << 12); }`：当传入 `(address)-1` 时，`lower = -1` 触发 `upper += 0x1000` 使 `upper = 0`，发射 `lui Rd, 0` + `addi Rd, Rd, -1`（此时 `Rd = -1`），随后连续两次 `slli + addi` 保持全 1，执行结果与 `get_target_of_movptr` 解码结果均为精确的 **`(address)-1`（`0xffffffffffffffff`）**！
3. 将非地址类的 64 位算术位掩码（如 [`interp_masm_riscv.cpp:401, 645`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp#L401) 和 [`sharedRuntime_riscv.cpp:1509`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp#L1509) 中的页对齐掩码 `7 - os::vm_page_size()`、[`stubGenerator_riscv.cpp:452-454`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp#L452-L454) 中的 `Universe::verify_oop_mask()`）统一改用通用 64 位立即数加载 `li` 而非地址专用的 `movptr`。

---

如果未来要在 OpenJDK 8u 中移植另一个全新的指令集架构（如某个专有 ASIC、DSP 或新一代开放架构），可直接按照本工程总结的核对清单推进 Phase 1：

```
[Phase A: 构建系统与交叉编译链]
  [ ] 1. 搭建 Docker 交叉编译环境，准备目标架构 sysroot 与 GNU 工具链
  [ ] 2. 在 common/autoconf/platform.m4 中注册 canonical triple (VAR_CPU, VAR_CPU_ARCH)
  [ ] 3. 在 hotspot/make/linux/ 创建 platform_<arch> 与 makefiles/<arch>.make
  [ ] 4. 修改 saproc.make 与 sa.make，暂时剔除 SA 调试库构建

[Phase B: 架构宏与共享层路由打通]
  [ ] 5. 在 macros.hpp 中定义 <ARCH>_ONLY 与 NOT_<ARCH>
  [ ] 6. 在 share/vm/runtime/globals.hpp, frame.hpp 等 20+ 共享头文件中注入路由
  [ ] 7. 在 os_linux.cpp 中增加 ELF EM_<ARCH> 机器码与大页默认尺寸

[Phase C: 架构核心数据结构定义]
  [ ] 8. 实现 register_<arch>.hpp/cpp (规范通用与浮点寄存器，禁止 zero 命名污染)
  [ ] 9. 实现 vmreg_<arch>.hpp/cpp (绑定硬件寄存器与 VMReg 标量槽位)
  [ ] 10. 实现 orderAccess_linux_<arch>.inline.hpp (自包含引用，严格对齐内存模型)
  [ ] 11. 实现 atomic_linux_<arch>.inline.hpp (AMO 优先，保障 ABA/活锁安全)
  [ ] 12. 实现 os_linux_<arch>.cpp/hpp (ucontext_t 寄存器提取与 Safepoint 缺页判定)
  [ ] 13. 实现 frame_<arch>.hpp/inline.hpp (栈帧布局、16字节对齐、回溯 sender)
  [ ] 14. 实现 javaFrameAnchor_<arch>.hpp 与 jniTypes_<arch>.hpp

[Phase D: 编译器桩与链接验收]
  [ ] 15. 实现 nativeInst_<arch>.hpp (提供 is_safepoint_poll 与 is_call_before 桩)
  [ ] 16. 创建 <arch>.ad，提供 CallStubImpl 与 HandlerImpl 最小实现
  [ ] 17. 提供 templateInterpreter_<arch>.hpp 中的 InterpreterCodeSize
  [ ] 18. 执行 ./docker/make.sh hotspot
  [ ] 19. 检查产物 build/.../libjvm.so 的 ELF 头与 nm -D 关键符号 (JNI_CreateJavaVM 等)
```

通过以上系统性适配与坑点规避，即可确保新架构以最短路径、最稳固的基线完成 `libjvm.so` 的编译与全量链接！
