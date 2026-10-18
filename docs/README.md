# OpenJDK 8u RISC-V Port 开发与设计文档体系

本项目旨在为 OpenJDK 8u（JDK 8 Updates）提供完整、高性能、工业级的 RISC-V 64位（`riscv64`）原生硬件架构端口（Port）。

本目录下的文档集专为人类开发者与 AI Coding Agent 设计，提供从环境搭建、架构决策、GC 屏障映射到分阶段实施路线的全套指导。

---

## 文档与任务看板索引

0. [项目任务总看板与实施追踪 (TASK-BOARD.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/TASK-BOARD.md)
   * 全局阶段进度矩阵（Phase 0 ~ Phase 6 Master Kanban）。
   * 各阶段编码前专属子看板（Sub-Boards）、细粒度 Sprint 卡片与执行 Checklist。

1. [项目背景与代际断层分析 (01-PROJECT-OVERVIEW-AND-BACKGROUND.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/01-PROJECT-OVERVIEW-AND-BACKGROUND.md)
   * 为什么需要在 2026 年开发 JDK 8u RISC-V Port？
   * 官方 RISC-V 端口（JEP 422）演进现状与 JDK 8u 的历史断层。
   * JDK 8 与现代 JDK（JDK 17/21+）在 HotSpot 核心架构上的四大断层。
   * 二进制兼容基线：为什么选用 Ubuntu 20.04 (Glibc 2.31) 作为构建与分发基准。

2. [Docker 交叉开发与调试环境搭建指南 (02-DEV-ENVIRONMENT-SETUP.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/02-DEV-ENVIRONMENT-SETUP.md)
   * 面向 Apple Silicon Mac (aarch64) 的轻量化容器方案。
   * 三级智能网络穿透策略（直连 -> 本地代理 -> 国内镜像兜底）与 AI Coding Agent 新人引导规范（SOP）。
   * 多架构工具链配置（GCC 9.4 交叉编译、Boot JDK 8、目标架构运行库）。
   * 自动化脚本工具箱使用手册（`configure.sh`, `make.sh`, `qemu-java.sh`, `qemu-gdb.sh`）。
   * QEMU 用户态仿真与 GDB Multiarch 指令级单步调试流水线。

3. [系统架构与子系统设计方案 (03-ARCHITECTURE-AND-DESIGN.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/03-ARCHITECTURE-AND-DESIGN.md)
   * “双镜像”移植策略：以 AArch64 (JDK 8) 为拓扑模板，以 RISC-V (JDK Mainline) 为指令/微架构源泉。
   * 汇编器（Assembler & MacroAssembler）与寄存器分配约定。
   * 内存模型（RVWMO）与并发原子操作（OrderAccess / Atomic）。
   * 全局安全点轮询（Global Safepoint Polling）与信号异常恢复机制。
   * 三大垃圾回收器屏障设计：Parallel Scavenge（卡表）、CMS（并发卡标记）、G1（SATB + Dirty Card 队列）。
   * 模板解释器（Template Interpreter）与 JIT 编译器（C1 / C2 riscv64.ad）设计。
   * 硬件扩展支持策略：RVC（16位压缩指令与缓存行对齐保护）、Zba/Zbb 位操作、RVV 1.0 矢量优化。

4. [分阶段里程碑与实施路线图 (04-ROADMAP-AND-MILESTONES.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/04-ROADMAP-AND-MILESTONES.md)
   * Phase 0 ~ Phase 6 的精确里程碑划分与验收准则。
   * 依赖甘特图与关键路径。
   * 质量保障、测试套件（jtreg, SPECjvm2008）与风险对策。

5. [QEMU on x86 仿真环境下的内存模型鸿沟与 RVWMO 漏洞掩盖深度解析 (05-QEMU-X86-MEMORY-MODEL-ANALYSIS.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/05-QEMU-X86-MEMORY-MODEL-ANALYSIS.md)
   * 什么是 QEMU TCG 动态二进制翻译机制？为什么执行指令的其实是物理 x86 芯片？
   * 硬件内存一致性模型代际鸿沟：x86 TSO（强模型）vs RISC-V RVWMO（极弱模型）。
   * 为什么在 x86 QEMU 下遗漏 `fence` 依然能 100% 跑通并发测试？“虚假安全性”的诞生与消解。
   * HotSpot JVM 致命实战反例：对象构造安全发布、GC 卡表写屏障与 `OrderAccess` 隐患。
   * 架构师应对策略：QEMU 快速迭代与真实 RISC-V 多核物理芯片双轨验证体系。

6. [为什么 QEMU 与 FPGA 仿真在物理与架构层面均无法验证多核弱内存并发（RVWMO）与生产级 JVM 稳定性 (06-WHY-QEMU-AND-FPGA-CANNOT-VALIDATE-CONCURRENCY.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/06-WHY-QEMU-AND-FPGA-CANNOT-VALIDATE-CONCURRENCY.md)
   * QEMU TCG 动态二进制翻译的基本块模型与执行本质。
   * FPGA 原型验证的三大物理死穴：10MHz vs 3GHz 频率陷阱、核心与主存时序“倒挂”、门级容量限制（无法容纳 64/128 核）。
   * QEMU 与 FPGA 必然漏检的四大类 JVM 核心缺陷（对象安全发布、GC 写屏障、JIT 代码 `fence.i` 广播、超多核 LR/SC 活锁）。
   * 数据中心芯片落地闭环：从 QEMU 图纸、FPGA 开机、回片 ES 实验室到生产金丝雀的四道防线体系。

7. [历史的血泪镜鉴：AArch64 服务器芯片商用化初期硬件 Errata 与 HotSpot JVM 弱内存并发缺陷深度实录 (07-HISTORICAL-SILICON-ERRATA-AND-JVM-CONCURRENCY-BUGS.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/07-HISTORICAL-SILICON-ERRATA-AND-JVM-CONCURRENCY-BUGS.md)
   * HotSpot 源码官方实锤：`vm_version_aarch64.cpp` 中直接放弃支持早期硬件工程样片（`guarantee(_variant != 0)`）。
   * OpenJDK JBS 官方五大战役实录：JDK-8179954 (C1/C2 volatile 屏障不一致破坏 JMM)、JDK-8248219 (解释器 fast_storefield 漏屏障报随机 SIGSEGV)、JDK-8369506 (推测读取引发堆损坏与 NPE)、JDK-8327647 (markWord 锁还原缺 LoadLoad 屏障)、JDK-8233839 (数组存根发布屏障缺失)。
   * 多核总线雪崩：Cavium 48~96 核独占监视器互锁与 LL/SC 活锁风暴，催生 ARMv8.1-A LSE 单指令原子革命。
   * Linux 内核官方 `silicon-errata.rst` 硬件勘误清单与补丁机制。
   * 对 OpenJDK 8u RISC-V Port 的四大生死警示与工程规范（AMO 指令首选、屏障镜像统一、动态代码修补、真实物理机 JCStress 检验）。
8. [HotSpot C++ 编码规范与工程铁律 (08-HOTSPOT-CODING-STYLE-AND-STANDARDS.md)](file:///Users/wujiahua/code/riscv-port-jdk8u/docs/08-HOTSPOT-CODING-STYLE-AND-STANDARDS.md)
   * 规范定位与历史脉络（从 OpenJDK Wiki 到现代 hotspot-style.md 演变）。
   * C++98 / C++03 语言基准与禁用现代特性黑名单（禁用 auto, nullptr, constexpr, lambdas, range-for, <atomic>, STL）。
   * 六大内存分配基类契约（`ResourceObj`, `CHeapObj<mtTag>`, `StackObj`, `ValueObj`, `AllStatic`, `MetaspaceObj` 与 NMT 追踪）。
   * 异常处理体系（绝对禁止 C++ `throw`/`catch`，HotSpot 原生 `TRAPS` / `CHECK` 宏范式）。
   * RTTI 禁令与标准库替代品（禁用 `typeid`/`dynamic_cast`，使用 `GrowableArray` 与 `outputStream`）。
   * 断言与防御性检查分级（`assert` vs `guarantee`，`fatal`，`ShouldNotReachHere`，`Unimplemented`）。
   * 并发与无锁编程铁律（RVWMO 屏障映射、AMO 单指令铁律杜绝活锁）。
   * 文件组织三段式（`.hpp` 声明, `.inline.hpp` 热点内联, `.cpp` 实体）与命名排版格式约束。
