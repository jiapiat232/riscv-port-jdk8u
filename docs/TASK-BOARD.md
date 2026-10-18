# OpenJDK 8u RISC-V Port 全周期任务总看板（Master Task Board）

> **看板说明**：  
> 本看板收录从 Phase 0 到 Phase 6 全周期所有大阶段的细粒度冲刺任务卡片（Sprint Cards）、责任模块、源文件归属与验收 Checklist。  
> 供人类开发者与 AI Coding Agent 查阅当前进展与后续各阶段的具体实施路径。

---

## 一、全局阶段进度矩阵（Master Overview）

| 阶段编号 | 阶段名称 | 状态 | 交付核心 | 验收关键指标 |
| :--- | :--- | :---: | :--- | :--- |
| **Phase 0** | 构建系统与 Docker 交叉编译闭环 | **`[x] 100% 已完成`** | 容器环境、三级网络路由、Autoconf/Makefiles | 生成 Makefile，精准进入源码依赖 |
| **Phase 1** | 底层汇编器、寄存器与 OS/CPU 粘合 | **`[/] 进行中`** | RV64GC 指令发射、寄存器系统、AMO/OrderAccess、Linux 信号 | `./docker/make.sh hotspot` 编译通过汇编与 OS 层 |
| **Phase 2** | 模板解释器与运行时 Stubs 跑通 | `[ ] 待开始` | 200+ 字节码模板、栈帧模型、call_stub、全局安全点 | `-Xint -version` 成功执行并打印 Hello World |
| **Phase 3** | C1 客户端编译器与 Parallel/Serial GC | `[ ] 待开始` | C1 线性扫描寄存器分配、LIR 汇编、PS 卡表写屏障 | 开启 `-XX:TieredStopAtLevel=1` 稳定运行大对象分配 |
| **Phase 4** | CMS 与 G1 垃圾回收器写屏障集成 | `[ ] 待开始` | CMS store-check 屏障、G1 SATB 预写屏障、跨 Region 脏卡队列 | 跑通 `jtreg:gc/g1` 与 `gc/cms` 并发标记测试 |
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

## 三、【Phase 1】底层汇编器、寄存器与 OS/CPU 粘合（进行中）

* **阶段定位**：HotSpot RISC-V 硬件机器码发射底座与操作系统底层线程粘合层
* **阶段状态**：`[/] 进行中`

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
* **状态**：`[ ] 待开始`
* **对应源码清单**：
  * [`hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.hpp)
  * [`hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/os_cpu/linux_riscv/vm/os_linux_riscv.cpp)
* **核心验收 Checklist**：
  - [ ] 适配 Linux RISC-V `ucontext_t` / `sigcontext`，准确提取崩溃点 PC 指针（`uc_mcontext.__gregs[REG_PC]`）；
  - [ ] 捕获 `SIGSEGV`、`SIGBUS`、`SIGILL`、`SIGFPE` 并转换为 JVM 内部异常处理；
  - [ ] 实现全局安全点轮询页（Global Safepoint Polling Page）缺页命中判断与跳转；
  - [ ] 实现 `os::current_frame()` 栈回溯与帧指针遍历。

### 【Sprint Card 1.4】核心指令编码器与宏汇编器（Assembler & MacroAssembler）
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **状态**：`[ ] 待开始`
* **对应源码清单**：
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/assembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/assembler_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/macroAssembler_riscv.cpp)
  * [`hotspot/src/cpu/riscv/vm/vm_version_riscv.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.hpp)
  * [`hotspot/src/cpu/riscv/vm/vm_version_riscv.cpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/vm_version_riscv.cpp)
* **核心验收 Checklist**：
  - [ ] 完整实现 RV64I/M/A/F/D 标准所有指令类型的位域编码函数（`emit_int32` / R/I/S/B/U/J）；
  - [ ] 移植宏指令（`MacroAssembler`）：
    - `movptr`：支持 64 位宽指针/常量的多指令加载序列（`lui` + `addi` + `slli` 等）；
    - `call_VM`：建立标准的 C++ ABI 调用桥接；
    - `safepoint_poll`：适配 JDK 8 的基于全局页读取触发信号机制；
  - [ ] CPU 特性探测器（`vm_version_riscv`）解析 Linux HWCAP 与 JVM 命令行参数联动。

### 【Sprint Card 1.5】编译排错与微型汇编发射单测（Build & TDD Unit Tests）
* **责任模块**：构建与测试验证
* **状态**：`[ ] 待开始`
* **测试与验证 Checklist**（借鉴 AArch64 `aarch64-asmtest.py` 与主线 `test_assembler_riscv.cpp`）：
  - [ ] **TDD 梯队 1（汇编器差分对比）**：以 GNU `riscv64-linux-gnu-as` 为 Oracle，比对 HotSpot 发射的 32 位机器码与标准机器码一致；
  - [ ] **TDD 梯队 2（内存即时执行）**：利用 `BufferBlob` 发射机器码并在 QEMU 中直接执行函数指针，验证运算结果；
  - [ ] **TDD 梯队 3（多线程原子操作单测）**：多线程并发压测 `Atomic::add` 与 `cmpxchg`；
  - [ ] **编译验收**：运行 `./docker/make.sh hotspot`，增量排错直到 Phase 1 所有目标文件编译通过；
  - [ ] **符号验证**：验证生成的 `libjvm.so` 中包含合法的 RISC-V ELF 符号。

---

## 四、【Phase 2】模板解释器与运行时 Stubs（字节码跑通）

* **阶段定位**：实现纯解释器模式（`-Xint`）下的完整运行，打通 Java 字节码执行闭环
* **阶段状态**：`[ ] 待开始`

### 【Sprint Card 2.1】运行时基础 Stubs 生成器
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`stubGenerator_riscv.cpp`, `stubRoutines_riscv.hpp/cpp`
* **核心验收 Checklist**：
  - [ ] 实现 `call_stub`：建立 C++ 调用 Java 解释器入口的桥接栈帧（保存 C++ 被调用者保存寄存器，传递参数）；
  - [ ] 实现 `catch_exception_entry` 与异常分派桩；
  - [ ] 实现原子与安全内存复制存根（`atomic_copy_stubs`）。

### 【Sprint Card 2.2】解释器栈帧管理与方法调用 ABI
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`frame_riscv.hpp/cpp`, `interp_masm_riscv.hpp/cpp`, `sharedRuntime_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 建立标准 HotSpot 解释器栈帧（`fp`, `ra`, `xbcp (s8)`, `xlocals (s7)`, `xmethod (s9)`）；
  - [ ] 实现 `InterpreterMacroAssembler` 栈顶缓存（ToS - Top of Stack Cache）管理；
  - [ ] 实现 Java 到 JNI 本地方法调用的转接桩（Java-to-Native / Native-to-Java）。

### 【Sprint Card 2.3】模板解释器核心表与 200+ 字节码模板
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`templateTable_riscv_64.cpp`, `templateInterpreter_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 逐一实现 200+ 个 Java 字节码指令的汇编模板（加载、存储、常量压栈、算术逻辑、分支跳转、对象创建与字段访问）；
  - [ ] 实现 `invokevirtual`, `invokestatic`, `invokespecial`, `invokedynamic` 方法分派；
  - [ ] 在 `putfield` / `putstatic` 中注入规范的 Release 屏障。

### 【Sprint Card 2.4】基于全局页的安全点轮询与自重写屏障
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`templateTable_riscv_64.cpp`, `macroAssembler_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 适配 JDK 8u 单一全局安全点轮询页机制（读取 `os::get_polling_page()` 触发 SIGSEGV）；
  - [ ] 在 `TemplateTable::patch_bytecode` 字节码自重写中强制插入数据依赖写写屏障（避免 JDK-8248219 随机崩溃）。

### 【Sprint Card 2.5】里程碑验收（Milestone 1）
* **核心验收 Checklist**：
  - [ ] 在 QEMU 下成功执行 `./docker/qemu-java.sh -Xint -version`；
  - [ ] 成功执行 `./docker/qemu-java.sh -Xint HelloWorld` 并正确打印输出。

---

## 五、【Phase 3】C1 客户端编译器与 Parallel/Serial GC

* **阶段定位**：打通一阶段 JIT 编译能力，大幅提升热点循环吞吐
* **阶段状态**：`[ ] 待开始`

### 【Sprint Card 3.1】C1 LIR 指令生成与线性扫描寄存器分配
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`c1_LIRGenerator_riscv.cpp`, `c1_FrameMap_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 定义 C1 虚拟寄存器映射与物理寄存器池掩码（Caller/Callee-saved 分配规则）；
  - [ ] 移植 HIR 到 LIR 的平铺发射规则。

### 【Sprint Card 3.2】C1 LIR 汇编器与运行时桩
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`c1_LIRAssembler_riscv.cpp`, `c1_Runtime1_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 实现 LIR 到 RISC-V 本地机器码的发射器；
  - [ ] 实现 C1 运行时存根（分配对象慢速桩、异常抛出桩、反优化 Trap 桩）。

### 【Sprint Card 3.3】Parallel Scavenge 与 Serial GC 卡表写后屏障
* **责任模块**：`hotspot/src/cpu/riscv/vm/`
* **对应源码**：`macroAssembler_riscv.cpp`, `c1_LIRAssembler_riscv.cpp`
* **核心验收 Checklist**：
  - [ ] 在解释器与 C1 写入堆对象字段后发射卡表后置脏标记（`byte_map_base + (addr >> 9) = 0`）；
  - [ ] 确保与 JDK 8 AArch64 的卡表写入策略完全对齐。

### 【Sprint Card 3.4】里程碑验收（Milestone 2）
* **核心验收 Checklist**：
  - [ ] 启用 `-XX:TieredStopAtLevel=1` 运行复杂循环，通过 `PrintAssembly` 观测到 C1 机器码；
  - [ ] 在 ParallelGC 下完成长时间大对象分配压力测试，无死锁、无泄漏。

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
