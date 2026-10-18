# 08 - HotSpot C++ 编码规范与工程铁律 (Coding Style & Standards)

## 一、规范定位与历史脉络 (Context & Evolution)

### 1.1 为什么 JDK 8u 仓库根目录没有内置编码文档？
在 OpenJDK 8（以及早期的 JDK 7、9、11）时期，HotSpot 虚拟机的 C++ 编码规范一直部署在 **OpenJDK 官方 Wiki**（`https://wiki.openjdk.org/display/HotSpot/StyleGuide`）上。
直到 JDK 14/15 期间，社区才发起文档工程整合，将长期运行在 Wiki 上的 HotSpot 规范正式导入源码树，形成上游参考仓库中的 [`/Users/wujiahua/code/jdk/doc/hotspot-style.md`](file:///Users/wujiahua/code/jdk/doc/hotspot-style.md)。

由于 JDK 8u 处于长期维护状态（LTS），其构建系统和源文件基准完全固定在 2014 年发布时的工业标准。为确保所有参与 RISC-V 端口移植的开发者和 Agent 具备高度一致的工程直觉与代码审查底线，本文档将 HotSpot 8 必须恪守的架构契约与编码规则系统化沉淀于此。

### 1.2 为什么 HotSpot 需要严苛的风格约束？
HotSpot 虚拟机是一个由数百万行底层 C++ 编写的极致高并发、低延迟系统。代码运行在裸机硬件与 JVM 字节码解释执行的边界上。任何随意的语言特权使用（如引入 STL 模板膨胀、C++ 异常或滥用 `auto`/`nullptr`）都会直接导致跨编译器兼容性崩溃、栈膨胀或严重的 GC/锁死故障。

---

## 二、语言基准与 C++ 特性黑白名单 (Language Standard & Features)

### 2.1 编译基准：严格限定为 C++98 / C++03
JDK 8u 的官方构建配置中，编译器编译参数并未指定 `-std=c++11`。代码必须严格兼容 GCC 4.8 至高版本 GCC 以及不同宿主环境。

### 2.2 绝对禁用的现代 C++ 特性清单（Banned Features）
以下特性在 JDK 8u HotSpot 源码中**一律禁止使用**，检出即视为构建阻断（Bug）：
* **禁用 `auto` 类型推导**：所有变量必须显式声明类型（如 `Register reg = ...`，`intx count = ...`），提升代码在无 IDE 符号跳转下的可读性。
* **禁用 `nullptr`**：必须统一使用经典宏 `NULL`。
* **禁用 `constexpr`**：常量必须使用 `const`、枚举（`enum`）或宏定义。
* **禁用范围 for 循环（Range-based for）**：必须使用经典下标循环或指针步进循环。
* **禁用 Lambda 表达式与闭包**：必须使用命名辅助类或仿函数。
* **禁用右值引用与移动语义（Rvalue References & Move Semantics）**：不得出现 `std::move`、`&&` 等。
* **禁用变长模板参数（Variadic Templates）**。
* **禁用现代 C++ 并发库**：严禁包含 `<atomic>`, `<thread>`, `<mutex>`, `<condition_variable>`。原子操作与内存屏障必须严格使用 HotSpot 原生宏与接口（[`atomic.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/runtime/atomic.hpp), [`orderAccess.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/runtime/orderAccess.hpp)）。

### 2.3 允许使用的 C++ 特性
* 模板函数与模板类（单参数或明确参数的特化，不可过度偏特化导致二进制膨胀）；
* 虚函数继承体系（仅限于允许带有虚表指针 `_vptr` 的重量级类，极小数据结构严禁引入虚表）；
* 基础宏元编程（如寄存器声明宏 `CONSTANT_REGISTER_DECLARATION`）；
* 内联函数（`inline`，必须置于 `.inline.hpp` 文件中）。

---

## 三、内存管理基类契约与 NMT 追踪 (Memory Allocation & NMT)

### 3.1 六大核心分配基类
HotSpot 严禁随意使用裸 `malloc`/`free` 或全局 `::new`。在 [`hotspot/src/share/vm/memory/allocation.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/memory/allocation.hpp#L62-L82) 中强制规定，VM 内部所有的 Class / Struct 必须显式继承自以下 6 大基类之一：

```
                    ┌──────────────┐
                    │    StackObj  │ (栈对象：禁用 operator new，仅限栈分配)
                    ├──────────────┤
                    │   ValueObj   │ (值对象：内嵌于其它对象中)
                    ├──────────────┤
                    │   AllStatic  │ (纯静态命名空间：构造函数私有，禁止实例化)
                    ├──────────────┤
                    │  ResourceObj │ (资源区对象：分配在 ResourceArea，生命周期由 ResourceMark 托管)
                    ├──────────────┤
                    │ CHeapObj<mt> │ (C 堆对象：底层通过 os::malloc 分配，受 NMT 内存追踪)
                    ├──────────────┤
                    │ MetaspaceObj │ (元空间对象：类元数据，存放在 Metaspace 中)
                    └──────────────┘
```

1. **`StackObj`**：
   - 典型代表：`MutexLocker`, `HandleMark`, `ResourceMark`。
   - 契约：其 `operator new` 被声明为私有，强制要求对象只能分配在 C++ 线程执行栈上，离开作用域自动执行析构函数（RAII 机制）。
2. **`AllStatic`**：
   - 典型代表：`OrderAccess`, `Atomic`, `Bytes`, `SharedRuntime`。
   - 契约：所有成员和方法均为 `static`，构造函数私有，禁止实例化任何对象。
3. **`ResourceObj`**：
   - 典型代表：编译期临时中间节点、短周期字符串。
   - 契约：分配在当前线程关联的 `ResourceArea` 内存块中，无须调用 `free`，当外层 `ResourceMark` 离开作用域时由系统整体一次性回滚释放。
4. **`CHeapObj<MemoryType>`**：
   - 典型代表：长期存活的虚拟机基础设施、全局数据表。
   - 契约：分配在操作系统堆中，必须指定 NMT 内存类型标签（如 `mtInternal`, `mtGC`, `mtCompiler`, `mtThread`）。必须显式通过 `delete` 释放，严禁内存泄露。
5. **`ValueObj`**：
   - 典型代表：`VMRegPair`, `Address` 等简单聚合体。
   - 契约：无虚表指针，随外层包装结构体的内存一同分配和释放。
6. **`MetaspaceObj`**：
   - 典型代表：`Method`, `ConstantPool`, `Klass`。
   - 契约：直接分配在 JVM 元空间中，由 GC 元数据管理器统一分配与清理。

### 3.2 严禁在共享数据结构中隐式引入虚表指针（`_vptr`）
在定义极基础、数量庞大、需要参与位运算或高频复制的轻量结构（如 `RegisterImpl`, `VMReg`, `Address`）时，**绝对禁止声明虚析构函数或虚方法**，避免产生多余的 8 字节 `_vptr` 开销及破坏 C 结构体内存布局。

---

## 四、异常处理体系（Exception Handling Without C++ Exceptions）

### 4.1 绝对禁止 C++ 原生异常
在 [`hotspot/src/share/vm/utilities/exceptions.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/utilities/exceptions.hpp#L32-L45) 中明确规定：
> **"We do not use C++ exceptions to avoid compiler dependencies and unpredictable performance."**

严禁在 HotSpot 代码中书写任何 `try`, `catch`, `throw` 语句。开启 C++ 异常会导致编译生成的 unwind 表急剧膨胀，并在跨平台（x86/ARM/RISC-V/MIPS）遇到编译器版本差异时引发严重的 ABI 崩溃。

### 4.2 HotSpot 的 TRAPS / CHECK 异常传递范式
HotSpot 拥有一套专属的、线程绑定的异常捕获与传递宏机制：
* **`TRAPS` 宏**：
  任何可能抛出 Java 异常的 C++ 函数，其最后一个参数必须显式声明为 `TRAPS`。
  ```cpp
  // 宏展开为：Thread* THREAD
  void resolve_and_patch(Method* m, TRAPS);
  ```
* **`CHECK` / `CHECK_NULL` / `CHECK_0` 宏**：
  在调用携带 `TRAPS` 的函数后，调用方必须**立刻**检查当前线程是否产生了未决异常（Pending Exception），若是则立即提前返回，将异常传递到外层调用栈：
  ```cpp
  // 调用示例
  Klass* k = SystemDictionary::resolve_or_fail(class_name, CHECK_NULL);
  
  // CHECK 宏的等价逻辑：
  // if (THREAD->has_pending_exception()) return NULL;
  ```
* **严禁静默吞掉异常**：
  若某段逻辑明确需要捕获并清除异常（如测试执行或可选加载），必须使用 `HAS_PENDING_EXCEPTION` 与 `CLEAR_PENDING_EXCEPTION`，并在代码中附带详细的架构审查注释。

---

## 五、RTTI 与标准库禁令 (No RTTI & No STL)

### 5.1 严禁 C++ RTTI
HotSpot 编译时全局开启了 `-fno-rtti`。
* **禁用操作符**：`typeid` 与 `dynamic_cast`；
* **规范化替代**：
  HotSpot 统一采用内置的虚方法标签系统：
  ```cpp
  // 正确：调用 HotSpot 显式类型查询
  if (obj->is_instance()) { ... }
  if (obj->is_method()) { ... }
  if (reg->is_valid()) { ... }
  ```

### 5.2 严禁标准模板库（STL）
HotSpot 核心代码中不得使用 `<vector>`, `<string>`, `<map>`, `<iostream>` 等。
* **容器替代品**：
  - 动态数组：使用 [`GrowableArray<T>`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/utilities/growableArray.hpp)（支持分配在 `ResourceArea` 或 `C-Heap`）；
  - 链表与哈希表：使用 `ResourceHashtable`, `KVHashtable` 或内联指针侵入式链表。
* **文本与格式化输出替代品**：
  - 严禁使用 `std::cout`, `std::cerr`, `printf`（特定底层调试除外）；
  - 必须使用 [`outputStream* st`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/utilities/ostream.hpp) 体系（如 `tty->print_cr(...)`, `stringStream ss`）。

---

## 六、断言与防御性自毁机制 (Assertions & Invariant Checking)

在 [`hotspot/src/share/vm/utilities/debug.hpp`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/utilities/debug.hpp#L153-L190) 中，HotSpot 建立了严格的防御性检查分级：

| 宏名 | 生效编译模式 | 失败行为 | 适用场景与工程准则 |
| :--- | :--- | :--- | :--- |
| **`assert(cond, msg)`** | **仅在 Debug / Fastdebug 生效** | 打印文件名、行号及错误描述，触发软中断断点（`BREAKPOINT`） | 用于高频内部状态校验、算法前置/后置条件，Product 构建会被彻底剔除，零性能开销。 |
| **`guarantee(cond, msg)`** | **全编译模式（包括 Product/Release）始终执行** | 打印错误信息并触发即时 JVM 崩溃 | 用于极其致命的跨系统契约、物理页越界保护、Linux 关键系统调用返回值校验。 |
| **`fatal(msg)`** | **全编译模式始终执行** | 立即打印致命错误并生成 core dump | 探测到不可恢复的物理级或严重逻辑破坏时的主动防御性自毁。 |
| **`ShouldNotReachHere()`** | **全编译模式始终执行** | 触发致命错误 | 放在不可达的 `default` 分支、循环终点，防止控制流逃逸。 |
| **`ShouldNotCallThis()`** | **全编译模式始终执行** | 触发致命错误 | 放在纯虚替代基类方法、或理论上绝不应该被调用的桩函数中。 |
| **`Unimplemented()`** | **全编译模式始终执行** | 打印函数名及行号并崩溃 | 阶段性开发时用于明确标识尚未移植的冷门路径。 |

---

## 七、并发与无锁编程铁律 (Concurrency & Lock-Free Protocols)

### 7.1 RVWMO 屏障映射铁律
在 RISC-V 弱内存模型下，任何跨线程可见的普通变量读写都不具备隐式保序性。
* 严禁依赖编译器自作主张的指令重排；
* 所有多线程可见变量发布，必须遵从 **`Release Barrier` $\rightarrow$ 写变量 $\rightarrow$ `StoreLoad/Full Barrier`** 的标准流水线；
* 所有多线程可见变量读取，必须通过 **读变量 $\rightarrow$ `Acquire Barrier`** 建立因果依赖。

### 7.2 原子操作 AMO 单指令铁律（杜绝活锁）
* 所有的原子累加（`Atomic::add`）和原子交换（`Atomic::xchg`），**必须使用 RISC-V 原生单指令 AMO（`amoadd.w/d.aqrl`, `amoswap.w/d.aqrl`）**；
* 严禁在基础原子操作中使用 `lr`/`sc` 重试循环，坚决杜绝 Cavium ThunderX 在 96 核心下暴露的缓存行抢占活锁灾难；
* 仅在 `Atomic::cmpxchg` 等必须判断旧值匹配的场景下使用双向屏障保护的 CAS 序列。

---

## 八、文件架构三段式与包含解耦 (File Structure & Decoupling)

为了避免上千个源文件引发头文件依赖爆炸（Header Bleed）与编译时间雪崩，HotSpot 严格遵从三段式划分：

```
                    ┌────────────────────────┐
                    │      foo_riscv.hpp     │  (类声明、外部枚举、常量定义)
                    └───────────┬────────────┘
                                │
                    ┌───────────▼────────────┐
                    │  foo_riscv.inline.hpp  │  (高频热点 inline 函数实现)
                    └───────────┬────────────┘
                                │
                    ┌───────────▼────────────┐
                    │      foo_riscv.cpp     │  (完整逻辑实现实体)
                    └────────────────────────┘
```

1. **`*.hpp`**：
   - 包含类的完整成员变量定义与公有方法签名；
   - 尽量使用前置声明（Forward Declaration，如 `class Thread;`, `class VMRegImpl;`）代替硬包含整个头文件；
   - 必须使用 `#ifndef ... #define ... #endif` 经典卫兵宏。
2. **`*.inline.hpp`**：
   - 实现需要在多个 `.cpp` 间内联共享的小型函数（如 `RegisterImpl::as_VMReg()`）；
   - 在需要使用这些内联函数的其他 `.inline.hpp` 或 `.cpp` 中显式包含；
   - 严禁在普通的 `*.hpp` 中包含 `*.inline.hpp`（防止形成包含回环）。
3. **`*.cpp`**：
   - 第一行必须是 `#include "precompiled.hpp"`（用于预编译头文件加速）；
   - 包含本模块的 `.hpp` 与 `.inline.hpp`。

---

## 九、命名风格与排版格式约束 (Naming & Formatting Conventions)

1. **类名（Class Names）**：
   - 采用大驼峰命名（`PascalCase`），如 [`RegisterImpl`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/cpu/riscv/vm/register_riscv.hpp#L59), [`OrderAccess`](file:///Users/wujiahua/code/riscv-port-jdk8u/hotspot/src/share/vm/runtime/orderAccess.hpp)；
   - 接口类或抽象类加 `Abstract` 前缀（如 `AbstractRegisterImpl`）。
2. **成员变量（Member Variables）**：
   - 统一采用下划线前缀加小写（`_var_name`），如 `_bitset`, `_encoding`；
   - 严禁出现无前缀或驼峰式成员变量，以便于在庞大的宏展开代码中立即区分局部变量与成员变量。
3. **成员方法与独立函数（Methods & Functions）**：
   - HotSpot 运行时代码广泛采用下划线小写风格（`snake_case`），如 `load_acquire()`, `is_valid()`；
   - C2 编译器与图节点部分保留了历史的驼峰风格（`camelCase`，如 `build_oop_map()`）；
   - 在各自模块内部保持“入乡随俗（When in Rome, do as the Romans do）”。
4. **常量与枚举（Constants & Enums）**：
   - 全局常量使用全大写加下划线（`ALL_CAPS`，如 `CSR_FFLAGS`）或小驼峰枚举；
   - 寄存器常量使用标准 ABI 小写别名（`ra`, `sp`, `a0`, `xthread`, `xheapbase`）。
5. **排版格式铁律**：
   - **缩进**：严格使用 **2 个空格** 缩进；
   - **严禁 TAB 字符**：源码中禁止出现 `\t`，一律展开为空格；
   - **大括号风格**：类与函数声明左大括号换行独占一行，控制流语句（`if`/`while`/`for`）左大括号同行；
   - **单行长度**：代码行宽推荐控制在 80~120 字符以内，长表达式在逻辑运算符处清晰换行。
