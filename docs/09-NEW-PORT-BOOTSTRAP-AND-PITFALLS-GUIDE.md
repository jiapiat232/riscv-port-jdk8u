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

## 五、新架构移植工程化核对清单（Step-by-Step Checklist）

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
