# support/ — 补充层契约（scaffold）

`support/` 是本脚手架的**第三条契约轴**：填补框架明确留白、但应用又必须按平台分叉的能力。它与 `osal/`（OS 抽象）、`hal/`（硬件抽象）**并列**；契约在 `support/`，逐平台后端在 `backends/support/<platform>/`。

## 为什么是"补充层"

设计原则：**层次去留 = 目标平台已提供能力 − 跨平台抽象需求**。

Zephyr / Linux 已提供 device 模型、DTS/DT、boot 编排，所以框架**不做** `bsp/`、`platform/`、自建设备模型。但下面这些东西**两端 API 形态不一致、又不属于 OS/HW 抽象**，框架主动留白——正是本补充层要填的"项目缺口"：

| 契约头 | `backends/support/linux` | `backends/support/zephyr` | 为什么是项目缺口 |
|---|---|---|---|
| `console.h` | stdout 输出 + SIGINT/SIGTERM 停止钩子 | 丢弃字节并计数，停止钩子只存不触发 | 终端输出 / 日志 sink 属应用侧 |
| `args.h` | 解析 `--config/--speed/--help/-h` | 无命令行，返回内置默认 | 启动参数形态是应用/平台约定 |
| `config_source.h` | 文件后端（`fopen` + `fread`） | 编译期内置文本 | 配置源是应用领域 |
| `can_bus.h` | `hal_can` 适配器（256 帧 RX 环 + 确定性发送） | 骨架 | 框架 `hal_can` 是异步契约；应用需要"一帧完成再发下一帧"的确定性 |

## 契约 ↔ 后端 配对

| 契约（`support/`，纯声明） | 后端（`backends/support/`） |
|---|---|
| `support/console.h` | `backends/support/<platform>/console.c` |
| `support/args.h` | `backends/support/<platform>/args.c` |
| `support/config_source.h` | `backends/support/<platform>/config_source.c` |
| `support/can_bus.h` | `backends/support/<platform>/can_bus.c` |

## 契约头规则

- 只放声明、类型、常量；**不放**任何 `.c`、OS 头、`#ifdef PLATFORM`。
- 只依赖 `osal/`、`hal/` 与 libc。
- 注释须写明：**为什么是项目缺口 + 各平台后端行为 + 所有权/返回值语义**。

## 骨架铁律

Zephyr 侧未实现的函数返回 `-ENOSYS`/`NULL`（或**有注释的计数丢弃** no-op）；**禁止**空 `return 0` 假装成功；**禁止** `#error` 让编译失败。

## 新增一个补充契约

1. 在 `support/` 写契约头（含上述注释规范）。
2. 实现双后端：`backends/support/linux/xxx.c`（真实）+ `backends/support/zephyr/xxx.c`（骨架）。
3. 加进 `CMakeLists.txt` 两个分支；必要时补 `cmake/zephyr_stubs` 假头。
4. 更新本文件与 `docs/architecture-spec.md`。

> 说明：`support/` 是**项目自建**的轴（上游框架没有），因此命名/内容由项目自行维护，无需与任何上游同步。

## 日志与抓包契约

`log_retention.h` 提供自动会话登记和租约释放：每个角色保留最新 3 次自动记录，捕获文件可追加到同一次登记。只接收已创建的自动路径；显式路径不登记。Linux 使用目录内归属标记、flock 和限定文件名清理；Zephyr 对非空登记返回 ENOSYS，无文件登记为空操作。释放必须晚于所有输出文件关闭，调用仅发生在初始化/退出线程。

`log_sink.h` 管理日志输出线程和文件，隔离 EasyLogger 与平台 I/O。`can_capture.h` 补充框架缺少的外部抓包进程生命周期：Linux 后端启动 candump、保存两路输出并回收子进程；Zephyr 启动返回 ENOSYS。应用通过纯契约启动、检查错误和停止，不直接调用 pthread、spawn、waitpid 或文件 API。抓包是进程内单实例，文件和子进程均由后端持有。
