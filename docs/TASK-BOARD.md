# OpenJDK 8u RISC-V Port 全周期任务总看板（Master Task Board）

> **看板说明**：  
> 本看板收录从 Phase 0 到 Phase 6 全周期所有大阶段的细粒度冲刺任务卡片（Sprint Cards）、责任模块、源文件归属与验收 Checklist。  
> 供人类开发者与 AI Coding Agent 查阅当前进展与后续各阶段的具体实施路径。

---

## 一、全局阶段进度矩阵（Master Overview）

| 阶段编号 | 阶段名称 | 状态 | 交付核心 | 验收关键指标 |
| :--- | :--- | :---: | :--- | :--- |
| **Phase 0** | 构建系统与 Docker 交叉编译闭环 | **`[x] 100% 已完成`** | 容器环境、三级网络路由、Autoconf/Makefiles | 生成 Makefile，精准进入源码依赖 |
| **Phase 1** | 底层汇编器、寄存器与 OS/CPU 粘合 | **`[x] 100% 已完成`** | RV64GC 指令发射、寄存器系统、AMO/OrderAccess、Linux 信号 | `./docker/make.sh hotspot` 编译通过汇编与 OS 层 |
| **Phase 2** | 模板解释器、锁机制、GC 屏障与 Safepoint 基座加固 | **`[x] 100% 已完成`** | 200+ 字节码模板、偏向锁/轻量级锁、四大 GC 解释器屏障、全局安全点与栈回溯 | `-Xint` 下四大 GC 跑通 `ComprehensiveTest` 与 RISC-V `javac` 自举编译 |
| **Phase 3** | C1 客户端编译器与 C1 级 GC 屏障 | `[ ] 进行中` | C1 线性扫描寄存器分配、LIR 汇编、C1 GC 写屏障与 Runtime1 桩 | 开启 `-XX:TieredStopAtLevel=1` 稳定运行大对象分配 |
| **Phase 4** | CMS 与 G1 垃圾回收器写屏障集成 | **`[x] 解释器/Stubs 已完成`** | CMS store-check 屏障、G1 SATB 预写屏障、跨 Region 脏卡队列 | 解释器与 `oop_arraycopy` 已 100% 跑通四大 GC，待合入 C1/C2 屏障 |
| **Phase 5** | C2 服务端编译器与 riscv64.ad 架构定义 | `[ ] 待开始` | 适配 JDK 8 ADLC 的 riscv64.ad、指令匹配、分层编译 | 成功开启 `-XX:+TieredCompilation` 稳定冷启动 Spring Boot |
| **Phase 6** | 现代硬件扩展加速与全套测试加固 | `[ ] 待开始` | RVC 4 字节对齐保护、Zba/Zbb 位操作、RVV 1.0 矢量化 | jtreg 核心通过率 $\ge 99\%$，SPECjvm2008 平稳 |

---

## 二、【Phase 0】构建系统与 Docker 交叉编译闭环（已完成 100%）

* **阶段定位**：基础设施建设与构建系统拓扑识别
* **阶段状态**：`[x] 已完成并提交 (Commit b1673ad192, e6d1cd467d)`

### 【Sprint Card 0.1】Docker 容器化交叉开发环境
* **责任模块**：`docker/`
* **状态**：`[x] 已完成`
* **对应文件**：[`docker/Dockerfile`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/Dockerfile), [`docker/shell.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/shell.sh)
* **Checklist**：
  - [x] 基于 Ubuntu 20.04（Glibc 2.31 基线）搭建多架构交叉编译容器；
  - [x] 安装 `gcc-riscv64-linux-gnu` (GCC 9.4) 与目标架构运行库；
  - [x] 集成宿主 Bootstrap JDK 8 (`openjdk-8-jdk-headless`)。

### 【Sprint Card 0.2】三级智能网络路由穿透
* **责任模块**：`docker/`
* **状态**：`[x] 已完成`
* **对应文件**：[`docker/build-image.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/build-image.sh)
* **Checklist**：
  - [x] Tier 1：探测 Ubuntu 官方源直连状态；
  - [x] Tier 2：自动嗅探 Mac 本地 Clash Verge 7897 / 7890 端口并穿透映射；
  - [x] Tier 3：无代理时自动平滑回退至中科大 USTC 镜像源；
  - [x] apt-get 增加 `-o Acquire::Retries=5` 抵御代理偶发 502。

### 【Sprint Card 0.3】Autoconf 平台探测与顶层配置生成
* **责任模块**：`common/autoconf/`
* **状态**：`[x] 已完成`
* **对应文件**：[`common/autoconf/platform.m4`](file:///Users/wujiahua/code/riscv-port-jdk8u/common/autoconf/platform.m4), [`common/autoconf/build-aux/config.sub`](file:///Users/wujiahua/code/riscv-port-jdk8u/common/autoconf/build-aux/config.sub), [`common/autoconf/generated-configure.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/common/autoconf/generated-configure.sh)
* **Checklist**：
  - [x] `config.sub` 支持 `riscv64-*` 三元组识别；
  - [x] `platform.m4` 增加 `riscv64` 目标 CPU 架构判定；
  - [x] 生成一致的 `generated-configure.sh`。

### 【Sprint Card 0.4】HotSpot Linux 构建系统 Makefile 适配
* **责任模块**：`hotspot/make/`
* **状态**：`[x] 已完成`
* **对应文件**：[`hotspot/make/defs.make`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/make/defs.make), [`hotspot/make/linux/makefiles/defs.make`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/make/linux/makefiles/defs.make), [`hotspot/make/linux/platform_riscv64`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/make/linux/platform_riscv64), [`hotspot/make/linux/makefiles/riscv.make`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/make/linux/makefiles/riscv.make)
* **Checklist**：
  - [x] 注册 `riscv` 与 `riscv64` 至 `SRCARCH`、`BUILDARCH` 与 `LIBARCH`；
  - [x] 创建 `platform_riscv64` 平台描述与 `riscv.make` 编译参数。

### 【Sprint Card 0.5】QEMU 用户态仿真与 GDB Multiarch 调试流水线
* **责任模块**：`docker/`
* **状态**：`[x] 已完成`
* **对应文件**：[`docker/configure.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/configure.sh), [`docker/make.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/make.sh), [`docker/qemu-java.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/qemu-java.sh), [`docker/qemu-gdb.sh`](file:///Users/wujiahua/code/riscv-port-jdk8u/docker/qemu-gdb.sh)
* **Checklist**：
  - [x] 验证 `./docker/configure.sh slowdebug` 100% 成功；
  - [x] 验证 `./docker/make.sh hotspot` 能正常触发编译并精准停在汇编源码缺失阶段；
  - [x] 建立 QEMU GDBServer 端口 1234 远程附加调试通道。

---

## 三、【Phase 1】底层汇编器、寄存器与 OS/CPU 粘合（已完成 100%）

* **阶段定位**：HotSpot RISC-V 硬件机器码发射底座与操作系统底层线程粘合层
* **阶段状态**：`[x] 100% 已完成`

### 【Sprint Card 1.1】CPU 寄存器系统（Register System）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码清单**：
  * [`hotspot/src/cpu/riscv/vm/register_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/register_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/register_definitions_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_definitions_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/registerMap_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/registerMap_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/vmreg_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vmreg_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/vmreg_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vmreg_riscv.inline.hpp)
  * [`hotspot/src/cpu/riscv/vm/vmreg_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vmreg_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 严格参照 AArch64（JDK 8）类签名，建立 `Register` 与 `FloatRegister` 的封装与枚举；
  - [x] 完整收录 32 个 64 位通用整数寄存器（`x0~x31`）及其 ABI 别名（`zero`, `ra`, `sp`, `gp`, `tp`, `t0~t6`, `s0~s11`, `a0~a7`）；
  - [x] 完整收录 32 个 64 位浮点寄存器（`f0~f31`）及其 ABI 别名（`ft0~ft11`, `fs0~fs11`, `fa0~fa7`）；
  - [x] 绑定 HotSpot 专用保留寄存器（与官方 OpenJDK RISC-V 上游规范对齐）：
    - `x23`（`s7` / `xthread` / `rthread`）固定分配给当前 `JavaThread*`；
    - `x27`（`s11` / `xheapbase` / `rheapbase`）固定分配给压缩指针基址 `narrow_oop_base`；
    - `x20~x22`, `x24~x26`, `x31` 分配给解释器专属寄存器（`esp`, `xdispatch`, `xbcp`, `xlocals`, `xmonitors`, `xcpool`, `xmethod`）；
  - [x] 实现 `Register::name()`、`FloatRegister::name()` 与 `VMRegImpl::set_regName()` 调试打印机制；
  - [x] 在共享头文件 `register.hpp`, `vmreg.hpp`, `registerMap.hpp` 注册 `TARGET_ARCH_riscv`。

### 【Sprint Card 1.2】OS/CPU 平台原子操作与内存屏障（Atomic & OrderAccess）
* **责任模块**：`hotspot/src/os_cpu/linux_riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码清单**：
  * [`hotspot/src/os_cpu/linux_riscv/vm/orderAccess_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/orderAccess_linux_riscv.inline.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/atomic_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/atomic_linux_riscv.inline.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/bytes_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/bytes_linux_riscv.inline.hpp)
  * [`hotspot/src/cpu/riscv/vm/bytes_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/bytes_riscv.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/copy_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/copy_linux_riscv.inline.hpp)
  * [`hotspot/src/cpu/riscv/vm/copy_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/copy_riscv.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/prefetch_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/prefetch_linux_riscv.inline.hpp)
* **核心验收 Checklist**：
  - [x] **严格遵循 RVWMO 规范实现 `OrderAccess`**：
    - `loadload()` / `loadstore()` / `acquire()` 映射 Acquire 屏障（`fence r, rw`）；
    - `storestore()` / `release()` 映射 Release 屏障（`fence rw, w`）；
    - `storeload()` / `fence()` 映射 Full Barrier（`fence rw, rw`）；
    - 全面适配 `load_acquire`、`release_store`、`store_fence`、`release_store_fence` 及其指针变体；
  - [x] **强制遵循 AMO 优先铁律实现 `Atomic`**：
    - `Atomic::add` 使用 RISC-V 原生单指令 AMO（`amoadd.w.aqrl` / `amoadd.d.aqrl`）；
    - `Atomic::xchg` 使用 RISC-V 原生单指令 AMO（`amoswap.w.aqrl` / `amoswap.d.aqrl`）；
    - `Atomic::cmpxchg` 采用标准双向屏障 CAS 序列（`__sync_val_compare_and_swap`）；
    - 绝不在高频原子加/交换中使用易引发活锁饥饿的 `lr`/`sc` 重试循环；
  - [x] 字节序反转（`bytes_riscv.hpp` & `bytes_linux_riscv.inline.hpp`）支持；
  - [x] 64 位机器字对齐原子内存复制（`copy_riscv.hpp` & `copy_linux_riscv.inline.hpp`）支持；
  - [x] 硬件预取桩（`prefetch_linux_riscv.inline.hpp`）支持；
  - [x] 在共享头文件 `orderAccess.inline.hpp`, `atomic.inline.hpp`, `copy.hpp`, `prefetch.inline.hpp` 及字节码处理类中挂载 RISC-V 目标。

### 【Sprint Card 1.3】Linux 操作系统与信号处理粘合（OS & Context Glue）
* **责任模块**：`hotspot/src/os_cpu/linux_riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码清单**：
  * [`hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.inline.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/vmStructs_linux_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/vmStructs_linux_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/vmStructs_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vmStructs_riscv.hpp)
* **核心验收 Checklist**：
  - [x] 适配 Linux RISC-V `ucontext_t` / `sigcontext`，准确提取崩溃点 PC 指针（`uc_mcontext.__gregs[REG_PC]`）；
  - [x] 捕获 `SIGSEGV`、`SIGBUS`、`SIGILL`、`SIGFPE` 并转换为 JVM 内部异常处理（包含 SafeFetch、Stack Overflow、Implicit Null Check、Implicit DivByZero、Unsafe Access 处理）；
  - [x] 实现全局安全点轮询页（Global Safepoint Polling Page）缺页命中判断与跳转；
  - [x] 实现 `os::current_frame()` 栈回溯、`os::get_sender_for_C_frame` 与 `os::fetch_frame_from_context` 帧指针遍历；
  - [x] 实现 `SpinPause()` 与全套 `_Copy_conjoint_*` 基础内存复制函数；
  - [x] 经 Docker RISC-V 交叉编译器（GCC 9.4，`-std=gnu++98`）独立编译成合法 RISC-V ELF 目标文件（`JVM_handle_linux_signal` 全局导出）。

### 【Sprint Card 1.4】核心指令编码器与宏汇编器（Assembler & MacroAssembler）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码清单**：
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/vm_version_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/vm_version_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 完整实现 RV64I/M/A/F/D 标准所有指令类型的位域编码函数（`emit_int32` / R/I/S/B/U/J）；
  - [x] 移植宏指令（`MacroAssembler`）：
    - `movptr`：支持 64 位宽指针/常量的多指令加载序列（`lui` + `addi` + `slli` 等）；
    - `call_VM`：建立标准的 C++ ABI 调用桥接；
    - `safepoint_poll`：适配 JDK 8 的基于全局页读取触发信号机制；
  - [x] CPU 特性探测器（`vm_version_riscv`）解析 Linux HWCAP 与 JVM 命令行参数联动。

### 【Sprint Card 1.5】编译排错与微型汇编发射单测（Build & TDD Unit Tests）
* **责任模块**：构建与测试验证
* **状态**：`[x] 已完成`
* **测试与验证 Checklist**（借鉴 AArch64 `aarch64-asmtest.py` 与主线 `test_assembler_riscv.cpp`）：
  - [x] **TDD 梯队 1（汇编器差分对比）**：以 GNU `riscv64-linux-gnu-as` 为 Oracle，比对 HotSpot 发射的 32 位机器码与标准机器码一致（66/66 全部通过）；
  - [x] **TDD 梯队 2（内存即时执行）**：编写 `docker/test-assembler.sh` 与 `docker/test_assembler.cpp` 在 QEMU 用户态执行并全绿通过；
  - [x] **TDD 梯队 3（多线程原子操作单测）**：多线程并发压测 `Atomic::add` 与 `cmpxchg`；
  - [x] **编译验收**：运行 `./docker/make.sh hotspot`，增量排错直到 Phase 1 所有目标文件编译通过并成功链接；
  - [x] **符号验证**：验证生成的 `libjvm.so` 中包含合法的 RISC-V ELF 符号（包含 `JNI_CreateJavaVM`, `JVM_InitProperties` 等）。

---

## 四、【Phase 2】模板解释器与运行时 Stubs（字节码跑通）

* **阶段定位**：实现纯解释器模式（`-Xint`）下的完整运行，打通 Java 字节码执行闭环
* **阶段状态**：**`[x] 100% 已完成 (Milestone 1 达成)`**

### 【Sprint Card 2.1】运行时基础 Stubs 生成器
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码**：
  * [`hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/stubRoutines_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubRoutines_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/stubRoutines_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubRoutines_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 实现 `call_stub`：建立 C++ 调用 Java 解释器入口的桥接栈帧（精确对齐 `entry_frame_call_wrapper_offset = -10`、`entry_frame_after_call_words = 35`、保存 C++ `xs0~xs11`、`fs0~fs11` 被调用者保存寄存器，传递参数）；
  - [x] 实现 `catch_exception_entry` 与异常分派桩（`forward_exception_entry`、`throw_exception` 运行时异常抛出桩）；
  - [x] 实现原子与安全内存复制存根（`generate_arraycopy_stubs` 支持全部 `jbyte/short/int/long/oop` 的 disjoint/conjoint 内存拷贝，对齐 `CardTableModRefBS` 写屏障）；
  - [x] 实现 SafeFetch 硬件缺页恢复桩（`SafeFetch32` 与 `SafeFetchN`）；
  - [x] 实现 `MacroAssembler::pd_patch_instruction` 分支跳转回填机制，70/70 TDD 单测全绿通过；
  - [x] 成功编译链接进 `libjvm.so`（耗时 1分05秒，0 错误 0 告警）。

### 【Sprint Card 2.2】解释器栈帧管理与方法调用 ABI
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码清单**：
  * [`hotspot/src/cpu/riscv/vm/frame_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/frame_riscv.inline.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.inline.hpp)
  * [`hotspot/src/cpu/riscv/vm/frame_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/interpreter_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interpreter_riscv.hpp)
  * [`hotspot/src/share/vm/interpreter/interpreter.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/interpreter/interpreter.hpp)
  * [`hotspot/src/cpu/riscv/vm/interp_masm_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/interpreterRT_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interpreterRT_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp)
* **核心验收 Checklist**：
  - [x] **建立标准 HotSpot 解释器栈帧管理机制**：
    - 在 `frame_riscv` 中实现 `sender_for_interpreter_frame`、`sender_for_entry_frame`、`sender_for_compiled_frame` 及统一栈回溯分派 `sender()`；
    - 严密对齐栈槽位偏移（`locals_offset = -8`, `bcx_offset = -9`, `initial_sp_offset = -10`, `sender_sp_offset = -3`, `last_sp_offset = -4`, `method_offset = -5`, `cache_offset = -7`）；
    - 实现栈遍历安全检测 `frame::safe_for_sender(JavaThread*)`，准确隔离 Guard 保护页、死循环与未对齐指针；
    - 实现动态反优化检查 `frame::adjust_unextended_sp()`、监控块寻址 `interpreter_frame_monitor_begin/end` 与执行结果回填 `interpreter_frame_result`；
  - [x] **实现 `InterpreterMacroAssembler` 栈顶缓存与字节码分派系统**：
    - 实现 ToS（Top of Stack Cache）压栈/弹栈（`push/pop(TosState)`、`push/pop_ptr/i/l/f/d`），支持 64 位机器字与双字对齐；
    - 实现高效字节码分派宏 `dispatch_next`、`dispatch_via`、`dispatch_base`，指令数优化至最简加载跳转；
    - 实现常量池与字节码解析宏（`get_unsigned_2_byte_index_at_bcp`、`get_cache_and_index_at_bcp`、`get_method_counters`）；
  - [x] **实现 Java / C 调用约定与运行时调用桥接桩**：
    - 遵循 RISC-V 标准 LP64D ABI 实现 `SharedRuntime::c_calling_convention` 与 `SharedRuntime::java_calling_convention`（8 个整型寄存器 `a0~a7` + 8 个浮点寄存器 `fa0~fa7`，超出部分 64 位栈对齐传递）；
    - 实现方法调用适配器桩生成（`generate_i2c2i_adapters` 中的 `i2c_entry` 与 `c2i_entry`）；
    - 实现 JNI 本地方法调用包装器生成器（`generate_native_wrapper`）与签名处理器（`SignatureHandlerGenerator`）；
    - 实现全局 Safepoint 桩（`generate_handler_blob`）、方法解析桩（`generate_resolve_blob`）、反优化桩（`generate_deopt_blob`）与罕见陷阱桩（`generate_uncommon_trap_blob`）；
  - [x] **编译链接验证通过**：
    - `./docker/make.sh hotspot` 成功编译全部 frame、interp_masm、interpreterRT 与 sharedRuntime 源码并完成 `Linking vm...`，产出 23MB 的 ELF `libjvm.so`；
    - 70/70 TDD 单测比对 GNU as 100% PASS。

### 【Sprint Card 2.3】模板解释器核心表与 200+ 字节码模板
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码**：[`hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp), [`hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 逐一实现 200+ 个 Java 字节码指令的汇编模板（加载、存储、常量压栈、算术逻辑、分支跳转、对象创建与字段访问）；
  - [x] 实现 `invokevirtual`, `invokestatic`, `invokespecial`, `invokedynamic` 方法分派；
  - [x] 修复 `checkcast` 与 `instanceof` 中对表达式栈（`esp`）的 `push`/`pop` 维护；
  - [x] 在 `putfield` / `putstatic` 中注入规范的 Release 屏障。

### 【Sprint Card 2.4】基于全局页的安全点轮询与自重写屏障
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码**：[`hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp), [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 适配 JDK 8u 单一全局安全点轮询页机制（读取 `os::get_polling_page()` 触发 SIGSEGV）；
  - [x] 在 `call_VM_leaf_base` 中强制保护 `t0` 与 `xmethod`（`t6`），杜绝 C++ ABI 覆盖；
  - [x] 修复 `remove_activation`，完整支持 `synchronized` 方法解锁与结构化锁校验；
  - [x] 补齐 `Interpreter::_remove_activation_entry` 异常栈展开例程；
  - [x] 在 `TemplateTable::patch_bytecode` 字节码自重写中强制插入数据依赖写写屏障（避免 JDK-8248219 随机崩溃）。

### 【Sprint Card 2.5】里程碑验收（Milestone 1 达成）
* **核心验收 Checklist**：
  - [x] 在 QEMU 下成功执行 `./docker/qemu-java.sh -Xint -version`（100% 成功输出版本号并以 0 退出）；
  - [x] 成功执行 `./docker/qemu-java.sh -Xint Hello` 并正确打印 `Hello, RISC-V OpenJDK 8u!`；
  - [x] 成功执行多场景压力单测 `ComprehensiveTest`（包含算术递归、嵌套 Synchronized、异常捕获展开、HashMap 集合遍历），全部 PASS！

### 【Sprint Card 2.6】底层运行时基座全方位加固（Safepoint / OopMap / 四大 GC 屏障 / 偏向锁与快速栈锁）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码**：
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/interp_masm_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/frame_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/frame_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/sharedRuntime_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/stubGenerator_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateInterpreter_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/templateTable_riscv.cpp)
* **核心验收 Checklist**：
  - [x] **G1GC / CMS / Parallel / Serial 四大 GC 写屏障完备化**：
    - 重构 `g1_write_barrier_pre` 与 `g1_write_barrier_post`，消除 `t0` 寄存器别名冲突，实现 16 字节对齐的 `push_reg(RegSet, sp)` / `pop_reg(RegSet, sp)`；
    - 修正 `load_byte_map_base` 中的 `bs->is_a(BarrierSet::CardTableModRef)` 多态类型断言；
    - 在 `stubGenerator_riscv.cpp` 中完整实现 `gen_write_ref_array_pre_barrier` 与 `gen_write_ref_array_post_barrier`，修复 `generate_oop_copy` 寄存器存活问题；
  - [x] **全局 Safepoint 与编译帧/Stub 帧栈回溯修复**：
    - 修复 `frame::safe_for_sender` 与 `frame::sender_for_compiled_frame` 中 `saved_fp` 偏移（`sender_sp + frame::link_offset`）；
    - 修复 `RegisterSaver::save_live_registers` 中 `OopMap` 4 字节栈槽（`BytesPerInt`）单位计算与 `generate_handler_blob` 的 `retaddr` PC 绑定；
  - [x] **偏向锁（Biased Locking）与轻量级栈锁（Fast Stack Locking）汇编快路径**：
    - 实现 `cmpxchgptr`、`cmpxchg_obj_header`、`load_prototype_header`、`biased_locking_enter` 与 `biased_locking_exit`；
    - 实现 `InterpreterMacroAssembler::lock_object` 与 `unlock_object` 快路径（解决 `BiasedLockingStartupDelay=0` 或运行 >4s 后触发 `ObjectSynchronizer::fast_exit` 致命断言问题）；
  - [x] **JNI Native 出口加固**：
    - 修复 `generate_native_entry` 出口处误调 `remove_activation(vtos)` 导致的 `synchronized native` 双重解锁崩溃；
  - [x] **严苛验证通过**：
    - 四大 GC（Serial、Parallel、CMS、G1）+ 立即偏向锁（`-XX:BiasedLockingStartupDelay=0`）/ 禁用偏向锁（`-XX:-UseBiasedLocking`）下 100% 跑通多线程竞争、偏向锁撤销 Safepoint、`Object.wait/notify`、`System.gc()`、`WeakReference` 回收；
    - 成功在 QEMU 下用 RISC-V 自身的 `javac`（`-J-Xint -J-XX:+UseG1GC -J-XX:BiasedLockingStartupDelay=0`）完成 Java 源码自举编译！

---

## 五、【Phase 3】代码修补重定位层与 C1 客户端编译器

* **阶段定位**：打通代码修补/重定位/蹦床底座与一阶段 JIT 编译能力（C1），大幅提升热点循环吞吐
* **阶段状态**：`[ ] 进行中（已完成 Sprint Card 3.0，正在推进 Sprint Card 3.1）`

### 【Sprint Card 3.0】代码修补、重定位与内联缓存/蹦床层（JIT 前置基座）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 已完成`
* **对应源码**：
  * [`hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/nativeInst_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/relocInfo_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/relocInfo_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/compiledIC_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/compiledIC_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/icBuffer_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/icBuffer_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 解耦定长 6 指令（24 字节）可修补 `MacroAssembler::movptr` 与变长立即数加载 `MacroAssembler::li`；
  - [x] 实现定长 2 指令（8 字节：`lui + addiw`）`li32`，支持 `set_narrow_oop` / `set_narrow_klass` 与 `patch_oop`；
  - [x] 修复 `mov_metadata` 与 `movoop` 中漏调 `code_section()->relocate(pc(), rspec)` 的隐患；
  - [x] 完整实现 `Assembler::extract / sextract / patch` 与 `MacroAssembler::pd_patch_instruction_size / target_addr_for_insn / pd_call_destination`；
  - [x] 完整实现 `NativeCall`（4 字节 `jal`）、`NativeCallTrampolineStub`（`auipc + ld + jr + 64位目标`）、`NativeMovConstReg`、`NativeJump`、`NativeGeneralJump`、`NativeIllegalInstruction`；
  - [x] 完整实现 `MacroAssembler::trampoline_call`、`emit_trampoline_stub`、`ic_call`、`emit_static_call_stub`；
  - [x] 完整实现 `Relocation::pd_set_data_value`、`pd_call_destination`、`pd_set_call_destination`、`pd_address_in_code`；
  - [x] 编译通过并在 `-Xint` 下回归验证 RISC-V `javac` 与 `ComprehensiveTest`。

### 【Sprint Card 3.1】C1 寄存器映射、C1 宏汇编、CodeStubs 与 Runtime1 桩
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[x] 100% 已完成`
* **对应源码**：[`c1_FrameMap_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_FrameMap_riscv.cpp), [`c1_MacroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_MacroAssembler_riscv.cpp), [`c1_CodeStubs_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_CodeStubs_riscv.cpp), [`c1_Runtime1_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/c1_Runtime1_riscv.cpp)
* **核心验收 Checklist**：
  - [x] 定义 C1 虚拟寄存器映射与物理寄存器池掩码（22 个可分配通用寄存器 `rnr 0..21` 与 10 个保留寄存器隔离）；
  - [x] 实现 `C1_MacroAssembler`（`inline_cache_check`、`build_frame`、`allocate_object`、`allocate_array`、`lock_object`、`unlock_object`）；
  - [x] 实现 `c1_CodeStubs_riscv.cpp` 与 `c1_Runtime1_riscv.cpp` 全部运行时存根（含 G1 Pre/Post Barrier Stub 与 `patch_code_riscv` 逆优化修补）。

### 【Sprint Card 3.2】C1 LIR 生成器与 LIR 汇编器
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`c1_LIRGenerator_riscv.cpp`, `c1_LIRAssembler_riscv.cpp`, `c1_LinearScan_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 移植 HIR 到 LIR 的平铺发射规则（`c1_LIRGenerator_riscv.cpp`）；
  - [ ] 实现 LIR 到 RISC-V 本地机器码的发射器（`c1_LIRAssembler_riscv.cpp`），并移除 `compiler_stubs_riscv.cpp` 中的 C1 桩。

### 【Sprint Card 3.3】C1 四大 GC（Serial / Parallel / CMS / G1）写屏障集成
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`c1_LIRGenerator_riscv.cpp`, `c1_CodeStubs_riscv.cpp`, `c1_Runtime1_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 在 C1 LIR 层完整对接 CardTable（Serial/Parallel/CMS）写后屏障与 G1 SATB 预写屏障 / Dirty Card 写后屏障。

### 【Sprint Card 3.4】里程碑验收（Milestone 2）
* **核心验收 Checklist**：
  - [ ] 启用 `-XX:TieredStopAtLevel=1` 稳定运行 `-version`、`Hello`、`ComprehensiveTest` 与 `javac`；
  - [ ] 在四大 GC 下通过 C1 JIT 编译执行压力测试，无死锁、无堆损坏。

---

## 六、【Phase 4】CMS 与 G1 垃圾回收器写屏障集成

* **阶段定位**：补齐现代企业级生产运行必备的低延迟并发与服务端垃圾回收能力
* **阶段状态**：`[ ] 待开始`

### 【Sprint Card 4.1】CMS（Concurrent Mark Sweep）并发卡标记支持
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`macroAssembler_riscv.cpp`, `c1_LIRAssembler_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 参照 AArch64 实现 CMS `store-check` 优化：
    ```riscv
    srli  t0, store_addr, 9
    add   t0, byte_map_base, t0
    lbu   t1, 0(t0)
    beqz  t1, .done
    sb    zero, 0(t0)
    .done:
    ```
  - [ ] 消除并发标记阶段的总线伪共享冲击。

### 【Sprint Card 4.2】G1 SATB 预写屏障（Pre-barrier）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`macroAssembler_riscv.cpp`, `c1_Runtime1_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 覆写旧引用前，读取旧值；若非空且并发标记激活，将旧引用压入 `JavaThread::satb_mark_queue`；
  - [ ] 队列满时跳转进入 `Runtime1::g1_pre_barrier_slow_id` 慢速处理存根。

### 【Sprint Card 4.3】G1 Dirty Card 跨 Region 写后屏障（Post-barrier）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`macroAssembler_riscv.cpp`, `c1_LIRAssembler_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 执行跨 Region 快速检测（`store_addr ^ new_val >= LogOfHRGrainBytes`）；
  - [ ] 若跨 Region，计算卡表地址并推入 `JavaThread::dirty_card_queue`，满时跳入慢速 Stub。

### 【Sprint Card 4.4】里程碑验收（Milestone 3）
* **核心验收 Checklist**：
  - [ ] 跑通 `jtreg test/hotspot/jtreg:gc/g1` 核心并发标记测试用例；
  - [ ] 跑通 `jtreg test/hotspot/jtreg:gc/concurrentMarkSweep` 核心用例。

---

## 七、【Phase 5】C2 服务端编译器与 riscv64.ad 架构定义

* **阶段定位**：实现终极 JIT 编译器 C2（Server Compiler），达到峰值生产性能
* **阶段状态**：`[ ] 待开始`

### 【Sprint Card 5.1】巨型架构描述文件 `riscv64.ad` 适配
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`hotspot/src/cpu/riscv/vm/riscv64.ad`
* **核心验收 Checklist**：
  - [ ] 裁剪现代主线引入的非 JDK 8 MachNode，适配 JDK 8 的 ADLC 预编译器语法；
  - [ ] 确保 ADLC 在编译期能顺利解析并生成 `ad_riscv64.cpp/hpp`。

### 【Sprint Card 5.2】指令匹配规则、操作数与寄存器掩码
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`hotspot/src/cpu/riscv/vm/riscv64.ad`
* **核心验收 Checklist**：
  - [ ] 定义完整的流水线资源模型（Pipeline Model）；
  - [ ] 定义通用寄存器、浮点寄存器的 RegMask；
  - [ ] 定义 64 位整数与浮点数、内存访问（Load/Store）的 Match 规则。

### 【Sprint Card 5.3】C2 级 G1/CMS 屏障内联与反优化处理
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`riscv64.ad`, `sharedRuntime_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 在 C2 理想图中内联匹配 G1 SATB 与卡表写屏障；
  - [ ] 实现反优化（Deoptimization）机器码修补（`NativeJump::patch_verified_entry`）与 I-Cache 刷新。

### 【Sprint Card 5.4】里程碑验收（Milestone 4）
* **核心验收 Checklist**：
  - [ ] 成功开启 `-XX:+TieredCompilation`（完整分层编译 0~4 级）；
  - [ ] 稳定冷启动并运行复杂企业级应用（如 Spring Boot 负载）。

---

## 八、【Phase 6】现代硬件扩展加速与全套测试加固

* **阶段定位**：全面对齐现代 RISC-V 扩展特性，完成工业级最终交付
* **阶段状态**：`[ ] 待开始`
### 【Sprint Card 6.1】RVC（16位压缩指令）并发修改安全加固
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **核心验收 Checklist**：
  - [ ] 严格审查所有 JIT 代码修补点（`patch_verified_entry`, IC-stubs），强制 4 字节自然对齐；
  - [ ] 在高频并发反优化压力测试中保证 0 次 `SIGILL`。

### 【Sprint Card 6.2】位操作扩展（Zba/Zbb）指令加速
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **核心验收 Checklist**：
  - [ ] 映射 `clz`, `ctz`, `cpop` 等硬件位计算指令；
  - [ ] 映射 `sh1add`, `sh2add`, `sh3add` 地址计算指令，优化数组索引与哈希表。

### 【Sprint Card 6.3】矢量化扩展（RVV 1.0）Intrinsics 加速
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **核心验收 Checklist**：
  - [ ] 实现基于向量指令的 `stubRoutines` 内存块复制（`arraycopy`）；
  - [ ] 实现 `String.equals`、`String.compareTo` 的向量化并行比对。

### 【Sprint Card 6.4】终极交付里程碑（Final Milestone）
* **核心验收 Checklist**：
  - [ ] `test/hotspot/jtreg` 核心用例通过率 $\ge 99\%$；
  - [ ] 成功运行 SPECjvm2008 与 DaCapo 基准套件，性能曲线平稳，达到生产级发布标准。

---

## 九、OpenJDK 8u RISC-V 移植编码规范与工程原则（Coding Standards & Engineering Rules）

### 1. 语言标准与编译器兼容性（C++98 / gnu++98）
* **严格遵循 GNU++98 基线**：OpenJDK 8u 的 HotSpot 统一采用 GCC `-std=gnu++98` 编译，**严禁使用任何 C++11 及更高版本语法特性**：
  - 严禁使用 `auto` 自动类型推导；
  - 严禁使用 `nullptr`（必须使用 `NULL`）；
  - 严禁使用 `constexpr`（必须使用 `const static` 或枚举）；
  - 严禁使用 基于范围的 for 循环（`for (auto x : vec)`）；
  - 严禁在类声明内直接初始化非静态/非常量成员变量。
* **跨编译器兼容**：代码需同时满足 GCC 与 Clang 的语法检查要求，杜绝非标准扩展。

### 2. 命名体系与符号定义（Naming Conventions）
* **类型与类名**：首字母大写的驼峰命名法（`CamelCase`），如 `MacroAssembler`, `RegisterImpl`, `OrderAccess`。
* **函数与方法名**：小写字母加下划线命名法（`snake_case`），如 `is_safepoint_poll()`, `patch_verified_entry()`。
* **私有与保护成员变量**：必须强制带有下划线前缀（`_leading_underscore`），如 `_encoding`, `_initialized`, `_cb`。
* **常量与宏定义**：全大写字母加下划线（`UPPER_SNAKE_CASE`），如 `VM_LITTLE_ENDIAN`, `EM_RISCV`。
* **架构别名隔离铁律**：避免全局作用域下的简写变量污染，如通用寄存器 `x0` 的别名统一采用 `zr`，**严禁在全局作用域定义 `const Register zero = x0;`**，防止与系统数学函数或外部库变量发生符号冲突。

### 3. 内存管理与对象基类层次（HotSpot Memory Subsystem）
* **严禁直接调用原生 `malloc` / `free` 或裸 `new` / `delete`**。
* 所有 C++ 类型必须显式继承自 HotSpot 特定的内存分配基类：
  - `AllStatic`：全静态工具类（杜绝实例化），如 `OrderAccess`, `Atomic`, `VM_Version`；
  - `StackObj`：仅允许栈上分配的临时资源类，如资源句柄、保护作用域；
  - `ResourceObj`：受 `ResourceMark` 统一管辖的临时内存块；
  - `CHeapObj<mtInternal / mtClass / mtGC>`：从 C 运行时堆分配的常驻元数据结构，带内存统计标记；
  - `VMObj`：作为嵌入其他结构的对象基类。

### 4. 架构隔离与条件编译宏（Architecture Decoupling）
* **目录边界清晰划分**：
  - `hotspot/src/cpu/riscv/vm/`：纯 RISC-V CPU 硬件、寄存器、汇编器与架构特定逻辑；
  - `hotspot/src/os_cpu/linux_riscv/vm/`：Linux OS 与 RISC-V 交叉粘合层（原子操作、信号捕获、上下文展开）；
  - `hotspot/src/share/vm/`：共享虚拟机内核，严禁硬编码架构指令。
* **标准架构条件编译**：
  - 共享层分支统一使用 [`macros.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/utilities/macros.hpp) 中定义的 `RISCV_ONLY(...)` 与 `NOT_RISCV(...)` 宏；
  - 针对头文件包含保护，统一遵循 `#ifdef TARGET_ARCH_riscv` 与 `#ifdef TARGET_OS_ARCH_linux_riscv` 标准。

### 5. 并发内存模型与原子操作铁律（RVWMO & AMO Iron Rules）
* **内存屏障精确映射**：严格依据 RISC-V Weak Memory Ordering (RVWMO) 硬件规范映射：
  - Acquire 屏障使用 `fence r, rw`（或 `__atomic_thread_fence(__ATOMIC_ACQUIRE)`）；
  - Release 屏障使用 `fence rw, w`（或 `__atomic_thread_fence(__ATOMIC_RELEASE)`）；
  - Full Barrier 使用 `fence rw, rw`（或 `__atomic_thread_fence(__ATOMIC_SEQ_CST)`）。
* **AMO 单指令优先**：在 `Atomic::add` 与 `Atomic::xchg` 中必须首选带 `.aqrl` 属性的原生单指令 AMO（`amoadd.w/d.aqrl`, `amoswap.w/d.aqrl`），坚决杜绝在无竞争高频点使用易发生活锁（Livelock）和缓存行饥饿的 `lr`/`sc` 循环。

### 6. 断言、防御性设计与代码格式化
* **全量断言护航**：关键路径前置条件与边界条件必须覆盖 `assert(condition, "message")`；涉及虚拟机致命状态判定使用 `guarantee`。
* **缩进与排版**：统一使用 2 空格缩进，**严禁包含制表符（Tab）**；行宽建议限制在 80~120 字符以内；大括号采用 K&R 风格。
