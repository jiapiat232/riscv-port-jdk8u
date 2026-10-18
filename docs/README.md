# OpenJDK 8u RISC-V Port 开发与设计文档体系

本项目旨在为 OpenJDK 8u（JDK 8 Updates）提供完整、高性能、工业级的 RISC-V 64位（`riscv64`）原生硬件架构端口（Port）。

本目录下的文档集专为人类开发者与 AI Coding Agent 设计，提供从环境搭建、架构决策、GC 屏障映射到分阶段实施路线的全套指导。

---

## 文档索引

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
