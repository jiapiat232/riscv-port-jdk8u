# 04 - 分阶段实施路线图与验收里程碑 (Roadmap & Milestones)

本文档制定了从底层汇编到 C2 编译器与全套 GC 支持的完整交付路径。随着工程实践深入，路线图根据真实踩坑与架构演进进行了动态调优，形成了一套可复用的 **“三位一体”跨代移植工程路线图**。

---

## 一、核心工程原则：“三位一体”移植范式与路线演进思考

在将 RISC-V 64-bit（RV64GC）移植到 OpenJDK 8u 的过程中，由于上游官方 RISC-V 端口直到 JDK 19（JEP 422）才正式合入主线，我们面临整整 **11 个大版本（JDK 8 $\leftrightarrow$ JDK 19）的虚拟机底层架构鸿沟**。为此，本项目确立了三大核心工程原则：

1. **架构与规范之锚（What & Why）——严格对齐本仓 JDK 1.8 AArch64 端口**：
   - 在类继承体系、函数签名、运行时协议（如全局单页 Safepoint 轮询、`OopMap` 栈回溯、偏向锁状态机、内联 `MacroAssembler` GC 写屏障、`CompiledStaticCall` 静态调用桩结构）上，100% 遵循 [`hotspot/src/cpu/aarch64/vm/`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/aarch64/vm/) 的设计。
2. **RISC-V 指令与编码之源（How in RISC-V）——严格参考 JDK 19 (`jdk-19-ga`) RISC-V 源码**：
   - 具体如何用 RISC-V 寄存器约定（`xthread=x23`, `xheapbase=x27`, `xmethod=x31`, `esp=x20`）、RVWMO 内存序（`fence` / `lr.d.aqrl` / `sc.d.rl`）、48 位地址定长加载（`movptr` 6 指令序列）、C1 LIR 生成器与 C2 `riscv64.ad` 规则来实现，直接参考本地 [`/Users/wujiahua/code/jdk19/src/hotspot/cpu/riscv/`](file:///Users/wujiahua/code/jdk19/src/hotspot/cpu/riscv/)（不看 JDK 19 之后演进过大的代码）。
3. **基座先行、小步快跑（Bottom-Up Hardening Before JIT）——关键路线调优**：
   - **原始路线图的误区**：原计划在解释器仅跑通 `-Xint -version` 和 Serial GC 后就立即启动 C1 编译器，将偏向锁、G1/CMS 写屏障留到 Phase 4。
   - **工程实践后的关键修正（新增 Phase 2.6 与 Phase 3.0）**：
     - Safepoint 栈回溯（`OopMap`）、偏向锁/轻量级栈锁 CAS、四大 GC（Serial/Parallel/CMS/G1）写屏障是**比 JIT 编译器更底层的运行时基座**。如果解释器与 Stubs 层面的 GC 屏障或栈回溯存在隐患，一旦叠加 C1/C2 动态机器码，排查难度将呈指数级上升。因此我们将四大 GC 屏障、全局 Safepoint、`OopMap` 与偏向锁全部前移至 **Phase 2.6**，并在 `-Xint` 下跑通 RISC-V `javac` 自举编译。
     - 在进入 C1 之前，必须先打通 **Phase 3.0（代码修补 `nativeInst`、重定位 `relocInfo`、内联缓存 `CompiledIC` 与远跳蹦床 `Trampoline`）**，解决“编译期 0 值占位发射 vs 运行期 48 位地址定长指令流回填”的架构鸿沟。

---

## 二、阶段里程碑全景图

```mermaid
flowchart TD
    P0["【Phase 0】构建脚手架与 Docker 交叉编译闭环<br/>(状态: 100% 已完成)"]
    P1["【Phase 1】底层汇编器、寄存器与 OS/CPU 粘合<br/>(状态: 100% 已完成, 66/66 GNU as 差分单测通过)"]
    P2_1["【Phase 2.1~2.5】模板解释器与基础运行时 Stubs<br/>(Milestone 1 已达成: -Xint -version / HelloWorld)"]
    P2_6["【Phase 2.6】底层运行时基座加固: 四大GC屏障/Safepoint/OopMap/偏向锁<br/>(Milestone 1.5 已达成: -Xint 四大GC跑通 javac 自举编译)"]
    P3_0["【Phase 3.0】代码修补、重定位与内联缓存/蹦床层<br/>(nativeInst / relocInfo / CompiledIC / 定长 movptr)"]
    P3_1["【Phase 3.1】C1 基础组件与 Runtime1 桩<br/>(FrameMap / C1_MacroAssembler / CodeStubs / Runtime1)"]
    P3_2["【Phase 3.2】C1 LIR 生成器与汇编器及四大GC屏障<br/>(Milestone 2: -XX:TieredStopAtLevel=1 稳定跑通)"]
    P4["【Phase 4】C1 & Runtime 并发 GC 与反优化高压验证<br/>(Milestone 3: 通过 G1/CMS 并发标记与偏向锁撤销压测)"]
    P5["【Phase 5】C2 服务端编译器与 riscv64.ad 架构定义<br/>(Milestone 4: 全功能分层编译 TieredCompilation)"]
    P6["【Phase 6】现代硬件特性: RVC加固、Zba/Zbb、RVV 1.0<br/>(Final Milestone: 达到生产级发布标准)"]

    P0 --> P1 --> P2_1 --> P2_6 --> P3_0 --> P3_1 --> P3_2 --> P4 --> P5 --> P6
```

---

## 三、各阶段详细任务、架构鸿沟与验收标准

### 【Phase 0：构建系统打桩与容器闭环】（已完成 100%）
* **目标**：在 Apple Silicon Mac 上搭建完整的 Docker 交叉编译与仿真环境，打通 autoconf 架构探测。
* **已交付物**：
  * [`docker/Dockerfile`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/Dockerfile) 与 6 个核心自动化工作流脚本；
  * [`common/autoconf/platform.m4`](file:///Users/wujiahua/code/riscv-port-jdk8u/common/autoconf/platform.m4) 注入 `riscv64` 探测；
  * [`common/autoconf/build-aux/config.sub`](file:///Users/wujiahua/code/riscv-port-jdk8u/common/autoconf/build-aux/config.sub) 解决 `riscv64-linux-gnu` 三元组识别；
  * [`hotspot/make/linux/platform_riscv64`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/make/linux/platform_riscv64) 与相关 makefile 骨架建立；
* **验收结果**：`./docker/configure.sh` 成功生成 Makefile，`./docker/make.sh hotspot` 精准进入源码依赖阶段。

---

### 【Phase 1：底层汇编器、寄存器与 OS/CPU 粘合】（已完成 100%）
* **核心目标**：建立 RISC-V 指令发射器与操作系统硬件线程粘合层。
* **已交付物**：
  1. 在 [`hotspot/src/cpu/riscv/vm/`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/) 建立：
     * [`register_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_riscv.hpp) / [`register_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_riscv.cpp)：定义通用寄存器（`x0~x31`）与浮点寄存器（`f0~f31`）元数据；
     * [`assembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp) / [`assembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.cpp)：移植基础 RV64I/M/A/F/D 机器码发射与打包逻辑；
     * [`macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp) / [`macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)：移植高频宏指令；
     * [`vm_version_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.hpp) / [`vm_version_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.cpp)：硬件能力探测；
  2. 在 [`hotspot/src/os_cpu/linux_riscv/vm/`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/) 建立：
     * [`atomic_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/atomic_linux_riscv.inline.hpp)：基于 AMO 与 LR/SC 指令的原子操作实现；
     * [`orderAccess_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/orderAccess_linux_riscv.inline.hpp)：基于 `fence` 指令的 RVWMO 内存屏障实现；
     * [`os_linux_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp)：Linux 信号处理（SIGSEGV, SIGBUS, SIGILL）与 `ucontext_t` PC 提取/修正。
* **验收结果**：66/66 项指令编码与 GNU `riscv64-linux-gnu-as` 差分比对 100% 一致，`libjvm.so` 首次成功编译链接。

---

### 【Phase 2：模板解释器、运行时 Stubs 与底层基座加固】（已完成 100%）

#### 1. Phase 2.1 ~ 2.5：模板解释器与字节码执行闭环（Milestone 1 已达成，Commit `a14d5214d0`）
* **已交付物**：
  * [`stubGenerator_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp)：`call_stub`、异常处理桩、`SafeFetch`、全类型 `arraycopy` 桩；
  * [`sharedRuntime_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp)：I2C/C2I 适配器、JNI Native Wrapper、Deopt / Uncommon Trap / Safepoint Handler Blob；
  * [`templateTable_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp) & [`templateInterpreter_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp)：200+ 个 Java 字节码模板与各类方法入口。
* **跨越的核心架构鸿沟**：
  * `sp`（16 字节对齐硬件栈）与 `esp`（8 字节槽位 Java 表达式栈）双栈解耦；
  * `call_VM_leaf_base` 对 `xmethod (t6/x31)` 调用者保存寄存器的跨 C++ 调用保护；
  * C++ 虚表调用中的 `vtable_index` 字节偏移 vs 机器字索引换算。

#### 2. Phase 2.6：Safepoint、OopMap、偏向锁与四大 GC 屏障基座加固（Milestone 1.5 已达成，Commit `32e0e50642`）
* **为什么在 C1 之前插入本阶段**：
  * JDK 19 移除了偏向锁（JEP 374）、改用了线程局部安全点轮询（JEP 312）并引入了 `BarrierSetAssembler`（JEP 304），而 JDK 8u 默认开启偏向锁、使用全局单页 Safepoint 轮询，且将 G1/CardTable 屏障直接内联在 `MacroAssembler` 中。必须在纯解释器下先把这些基座彻底跑通。
* **已交付物与跨越的架构鸿沟**：
  * **鸿沟 1（JDK 15 移除偏向锁 vs JDK 8u 默认开启偏向锁）**：从 JDK 8u AArch64 逆向移植 `biased_locking_enter/exit` 与 `InterpreterMacroAssembler::lock_object/unlock_object` 轻量级栈锁 CAS 快路径，解决 4 秒启动延迟后触发 `ObjectSynchronizer::fast_exit` 断言崩溃问题；
  * **鸿沟 2（JDK 11 `BarrierSetAssembler` vs JDK 8u 内联屏障）**：在 [`macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp) 与 [`stubGenerator_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp) 中完备实现 Serial、Parallel、CMS、G1 四大 GC 的字段写屏障与 `oop_arraycopy` 批量写屏障；
  * **鸿沟 3（Safepoint 栈回溯与 `OopMap` 粒度）**：修复 [`frame_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp) 中 `saved_fp` 偏移（`sender_sp - 2`）以及 [`sharedRuntime_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp) 中 `OopMap` 的 4 字节（`BytesPerInt`）VMReg 槽位单位；
  * **验收结果**：在 `-Xint` 下，四大 GC（Serial / Parallel / CMS / G1）+ 立即偏向锁模式 100% 跑通 [`ComprehensiveTest.java`](file:///Users/wujiahua/code/riscv-port-jdk8u/ComprehensiveTest.java) 及 **RISC-V `javac` 自举编译**！

---

### 【Phase 3：代码修补重定位层与 C1 客户端编译器】（当前推进阶段）

为了确保工程可控、步步为营，Phase 3 拆分为 3 个严格递进的子阶段：

#### 【Step 3.0：代码修补、重定位与内联缓存/蹦床层（JIT 前置基座）】（已完成 100%，Commit `70797efb1d`）
* **核心目标**：打通 JIT 编译代码生成、安装（`CodeBuffer::copy_code_to`）、重定位（`Relocation`）、内联缓存（`CompiledIC` / `icBuffer`）与远距离调用蹦床（`NativeCallTrampolineStub`）的全链路机器码修补能力。
* **已交付物**：
  * [`hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp) & [`nativeInst_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/relocInfo_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/relocInfo_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/compiledIC_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/compiledIC_riscv.cpp) & [`icBuffer_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/icBuffer_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp) & [`macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp) / [`macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
* **跨越的核心架构鸿沟**：
  * **定长可修补 `movptr`（固定 6 条指令 24 字节）vs 变长立即数加载 `li`（1~6 条指令）**：JIT 编译期常发射 `mov_metadata(reg, NULL)` 或 `movoop(reg, jobject)` 并在 `CodeSection` 记录重定位项，待安装或运行期通过 `NativeMovConstReg::set_data` / `Relocation::pd_set_data_value` 回填真实 48 位地址。若 `movptr(..., 0)` 收缩为单条 `mv reg, zr`，运行期按 24 字节回填将直接摧毁后续 5 条指令！
  * **`mov_metadata` / `movoop` 重定位项注册**：确保每次发射可修补元数据/对象引用时向 `code_section()->relocate(pc(), rspec)` 注册重定位记录，并支持压缩指针下的 `li32`（`lui + addiw` 8 字节）`patch_oop`。
  * **RISC-V `jal` $\pm 1\text{MB}$ 跳转范围限制与 `Trampoline` 蹦床桩**：AArch64 的 `bl` 拥有 $\pm 128\text{MB}$ 寻址范围，而 RISC-V 的单指令 `jal` 仅有 $\pm 1\text{MB}$ 范围！在默认最大 240MB 的 CodeCache 中，JIT 方法间调用与 Runtime 调用必须通过 `trampoline_call` + `NativeCallTrampolineStub`（`auipc + ld + jr + 64-bit target`）实现原子可修补远跳。

#### 【Step 3.1：C1 寄存器映射、宏汇编与 Runtime1 运行时桩】（已完成 100%）
* **核心目标**：建立 C1 编译器的栈帧布局、物理寄存器池分配规则、C1 专用宏汇编与 `Runtime1` 慢速路径 Stubs。
* **已交付物**：
  1. [`c1_FrameMap_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.hpp) / [`c1_FrameMap_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.cpp)：映射 22 个可分配通用寄存器（`rnr 0..21`）与 32 个浮点物理寄存器的 `LIR_Opr`，严格隔离 10 个保留寄存器（`xheapbase`, `xthread`, `fp`, `zr`, `ra`, `sp`, `gp`, `tp`, `t0`, `t1` 位于 `rnr 22..31`）；
  2. [`c1_MacroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_MacroAssembler_riscv.hpp) / [`c1_MacroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_MacroAssembler_riscv.cpp)：实现 C1 内联缓存校验 `inline_cache_check`、编译帧建立 `build_frame`、快速对象/数组分配 `allocate_object` / `allocate_array`、C1 快速锁 `lock_object` / `unlock_object`（集成偏向锁与 CAS 轻量级锁）；
  3. [`c1_CodeStubs_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_CodeStubs_riscv.cpp)：实现转换、范围检查、除零异常、`NewInstance`、`NewTypeArray`、`NewObjectArray`、`G1PreBarrierStub`、`G1PostBarrierStub`、`PatchingStub`、`MonitorEnter/Exit` 等全部 C1 尾置慢速桩；
  4. [`c1_Runtime1_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp)：生成全部 C1 `Runtime1` 存根（异常展开、对象分配、数组分配、卡表/G1 慢速屏障、反优化与逆优化式代码修补 `patch_code_riscv`）。
* **跨越的核心架构鸿沟**：
  * **JDK 19 `#ifdef DEOPTIMIZE_WHEN_PATCHING` vs JDK 8u `#ifndef TARGET_ARCH_aarch64`**：在 [`c1_Runtime1.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.hpp) / [`c1_Runtime1.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/c1/c1_Runtime1.cpp) 中将 `TARGET_ARCH_riscv` 纳入与 AArch64 相同的逆优化修补机制（`Runtime1::patch_code_riscv`），规避 RVWMO 弱内存序下跨核原地拷贝 24 字节指令流的撕裂风险；
  * **RISC-V `StubFrame` 顶部 `fp` vs AArch64 底部 `rfp` 偏移差**：精确对齐 `StubFrame::load_argument`（`[fp + offset*8]`）与异常/修补返回地址槽位（`[fp - 8]`），并闭合 `save_live_registers` 中 27 个整数寄存器的奇数对齐填充字与 `OopMap` / `restore_live_registers_except_r10` 的三方契约；
  * **JDK 8u vs JDK 19 `CodeStubs` 契约适配**：适配双参数 `RangeCheckStub`、无参 `DeoptimizeStub`、多态 `CounterOverflowStub::_method` 以及 JEP 304 之前的内联 `G1Pre/PostBarrierStub`。

#### 【Step 3.2：C1 LIR 生成器与 LIR 汇编器（Milestone 2）】
* **核心目标**：打通 HIR $\rightarrow$ LIR $\rightarrow$ RISC-V 机器码完整编译流水线。
* **具体任务**：
  1. `c1_LIRGenerator_riscv.cpp`：将平台无关 HIR 节点平铺为 RISC-V LIR 指令序列（含内置函数 Intrinsics、CAS、比较分支、算术运算与 GC 写屏障调用）；
  2. `c1_LIRAssembler_riscv.cpp`：将 LIR 指令发射为 RISC-V 机器码（含方法序言/尾声、OSR 栈帧迁移、异常/反优化 Handler、Safepoint 轮询点、常量池加载与 `trampoline_call`）；
  3. 从 [`compiler_stubs_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/compiler_stubs_riscv.cpp) 移除全部 C1 占位桩。
* **验收标准（Milestone 2）**：
  * 开启 `-XX:TieredStopAtLevel=1`（及 `-Xcomp -XX:TieredStopAtLevel=1`）成功运行 `-version`、`Hello` 与 [`ComprehensiveTest`](file:///Users/wujiahua/code/riscv-port-jdk8u/ComprehensiveTest.java)；
  * 在四大 GC（Serial / Parallel / CMS / G1）下均能稳定通过 C1 JIT 编译执行与 `javac` 编译测试。

---

### 【Phase 4：C1 & Runtime 并发 GC 与反优化高压验证】
* **核心目标**：在解释器 + C1 混合模式下，对 G1/CMS 并发标记、高频反优化（Deoptimization）、偏向锁批量重偏向/撤销、OSR（栈上替换）进行全方位压力验证与边界修复。
* **具体任务**：
  1. 验证 C1 `G1PreBarrierStub` / `G1PostBarrierStub` 在并发标记周期中的慢速队列刷新；
  2. 验证 C1 编译代码触发反优化（`UncommonTrap` / `Deoptimization`）回退至解释器时的栈帧解包（`frame::sender_for_compiled_frame` 与 `deopt_blob`）；
  3. 跑通复杂长稳压力测试与 `javac` 大规模编译任务。
* **验收标准（Milestone 3）**：
  * 在 `-XX:TieredStopAtLevel=1` 配合 `-XX:+UseG1GC` 与 `-XX:+UseConcMarkSweepGC` 下稳定完成高强度并发分配与 `javac` 编译，0 崩溃、0 堆损坏。

---

### 【Phase 5：C2 服务端编译器与 riscv64.ad 架构定义】
* **核心目标**：实现终极 JIT 编译器 C2（Server Compiler），达到与 x86-64 / AArch64 比肩的峰值运行性能。
* **具体任务**：
  1. 构建架构描述文件 `hotspot/src/cpu/riscv/vm/riscv64.ad`；
  2. 跨越 **JDK 8 ADLC vs JDK 19 ADLC 架构鸿沟**：裁剪 JDK 19 `riscv.ad` 中引入的非 JDK 8 MachNode（如 `CompressBits`、ZGC/Shenandoah 屏障、新 Vector API 节点），回填 JDK 8u 的 CardTable / G1 写屏障匹配规则；
  3. 定义完备的指令匹配规则（Instruction Match Rules）、操作数约束（Operands）、寄存器掩码（RegMask）与管道调度（Pipeline Models）。
* **验收标准（Milestone 4）**：
  * 成功启用完整的分层编译机制（`-XX:+TieredCompilation`）与纯 C2 模式（`-XX:-TieredCompilation`）；
  * 跑通复杂 Java 应用与基准测试负载。

---

### 【Phase 6：现代硬件扩展加速与全套测试加固】
* **核心目标**：全面对齐现代 RISC-V 硬件新特性，完成工业级质量验收。
* **具体任务**：
  1. **RVC（16位压缩指令）加固**：验证所有修补点强制 4 字节自然对齐；
  2. **位操作扩展（Zba/Zbb）**：打通 `clz`, `ctz`, `cpop`, `sh1add` 等高效指令映射；
  3. **矢量化扩展（RVV 1.0）**：实现基础 `arraycopy` 和字符串 Intrinsics 向量化加速。
* **验收标准（Final Milestone）**：
  * **jtreg 全量回归**：`test/hotspot/jtreg` 核心用例通过率 $\ge 99\%$；
  * **权威基准测试**：成功运行 SPECjvm2008 与 DaCapo 基准套件，性能曲线平稳。

---

## 四、风险控制与回退预案（Risk Management）

1. **RVC 并发修补风险**：
   * *预案*：JVM 初始调试与移植阶段，默认禁用 RVC（`-XX:-UseRVC`）。待 C1/C2 与反优化稳定性验证完毕后，再行开启。
2. **QEMU 用户态多线程内存模型仿真差异**：
   * *预案*：QEMU 默认在 x86/arm64 宿主上可能提供强于 RISC-V RVWMO 的内存序，隐藏部分漏掉的 `fence` 指令。为此在每个阶段严格对照 RVWMO 规范审查每一处 `fence` / `.aqrl` 原语，并在后续安排真实物理 RISC-V 硬件压测。
