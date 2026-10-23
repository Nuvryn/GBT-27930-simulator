# 架构规范 — Platform Scaffold

> 本文是脚手架的**规范性（normative）文档**：定义分层、目录、命名、契约/后端规则、include 策略、扩展流程与验证门禁。
> **修改架构前先读本文；新增目录/文件必须符合本文。**
> 关联：`README.md`（用法）、`support/README.md`（补充层契约说明）。

---

## 0. 快速规则清单（PR/评审用）

1. 它在正确的层吗？（契约→根目录三轴；实现→`backends/`；应用→`app/`；三方件→`third_party/`）
2. 契约头里**没有** `.c`、**没有** OS 头、**没有** `#ifdef PLATFORM`。
3. 平台差异靠 `-DPLATFORM` 选择**整组**后端文件，**不是**源内 `#ifdef` 分叉。
4. 跨模块头用**模块前缀**（`osal/…`、`hal/…`、`support/…`、`common/…`）；同模块私有头才用同目录裸引。
5. 三方件放 `third_party/<lib>/`，适配文件命名 `<lib>_port.c`。
6. 未实现的后端返回 `-ENOSYS`/`NULL`——**不静默成功**。
7. 不留运行时产物（`*.log`、`build*/`）；`.gitattributes` 强制 LF。
8. 提交前跑门禁：Linux 构建 + Zephyr 编译检查。

---

## 1. 设计原则（第一性）

### 1.1 层次去留公式

> **层次去留 = 目标平台已提供能力 − 跨平台抽象需求**

只抽象**两端平台 API 形态不一致**的地方；平台已提供的东西绝不重复造：

| 平台已提供 | 本脚手架**不做** |
|---|---|
| Zephyr device 模型 / Linux fd 抽象 | 不自建设备模型 |
| Zephyr DTS / Linux Device Tree | **不做 `bsp/` 板级层** |
| Zephyr init levels / Linux init/systemd | **不做 `platform/` 编排层** |

### 1.2 零 `#ifdef` 原则

平台选择只存在于**构建层**（CMake 选文件集）与**后端薄壳**。`app/` 中唯一允许的平台分支是**入口签名**（host `main(argc,argv)` vs Zephyr `main(void)`）——这是 support 层无法抽象的物理差异。

- ✅ `-DPLATFORM=linux|zephyr` 在 CMake 里选择后端文件集。
- ❌ 在 `.c` 里用 `#ifdef PLATFORM` 分叉**行为**。
- 特性宏（`#ifdef FEATURE_X`）由“链接了哪个后端”决定，是合法用法。

---

## 2. 分层与目录

### 2.1 三条契约轴 + 应用区

```
┌ 契约（根目录，纯声明）──────────────────────────────┐
│  osal/     OS 抽象（task/mutex/sem/mq/delay/mem）    │
│  hal/      硬件抽象（9 类外设 + 设备发现）           │
│  support/  补充层（console/args/config/can_bus）     │
└───────────────┬──────────────────────────────────────┘
                │ 一一配对
┌ 后端（实现）───▼──────────────────────────────────────┐
│  backends/osal/{posix,zephyr}                        │
│  backends/hal/{linux,zephyr}                         │
│  backends/support/{linux,zephyr}                     │
└──────────────────────────────────────────────────────┘
   app/         应用层（业务，零 #ifdef）
   third_party/ 三方件（可选）
```

### 2.2 契约 ↔ 后端 配对表

| 契约（根） | 后端 | 备注 |
|---|---|---|
| `osal/` | `backends/osal/{posix,zephyr}` | `posix` = API 家族 |
| `hal/` | `backends/hal/{linux,zephyr}` | `linux` = 平台名 |
| `support/` | `backends/support/{linux,zephyr}` | 补充层（项目自建） |

补充契约新增 `config_store.h`（Linux 原子保存 / Zephyr ENOSYS）与 `ui_display.h`（可选 Linux fbdev/evdev / Zephyr ENOSYS）；契约只使用路径、旋转等纯值，不依赖 app 或 LVGL 类型，平台后端由 CMake 选取。

补充契约还包括 `log_sink.h`（日志工作线程/文件）及 `can_capture.h`（外部 CAN 抓包生命周期）。Linux 使用平台线程与直接 spawn 管理，Zephyr 未实现的启动操作返回 ENOSYS；应用不感知进程和文件实现。

`log_retention.h` 补充自动会话归属、按角色保留最近 3 次记录和活动租约释放。Linux 后端负责目录锁、标记校验及限定文件删除，Zephyr 对文件操作返回 ENOSYS。应用只登记成功创建的自动文件路径，输出关闭后释放租约，不直接枚举目录或删除文件。

---

## 3. 命名规范

### 3.1 `backends/`（不叫 `ports/`、不叫 `platform/`）

| 名称 | 判定 | 理由 |
|---|---|---|
| `ports/` | ❌ | 需免责声明；与 BSD ports / 网络端口 / HAL 的 `port` 类型撞义 |
| `platform/` | ❌ | 与"不做 platform 编排层"原则冲突；只描述维度不描述角色 |
| `backends/` | ✅ | 准确表达"契约的逐平台薄壳后端"，无歧义 |

### 3.2 类型命名分层

- 公共契约/跨文件类型：全前缀 `<axis>_<thing>_t`（如 `hal_can_filter_t`）。
- 后端内部类型：带平台前缀 `<axis>_<platform>_<thing>_t`（如 `hal_linux_can_filter_t`）——用于与公共契约同名类型**消歧**。
- 文件局部静态类型：可用短名，但不得跨文件使用裸名。

### 3.3 文件命名

- 契约头：`osal/osal*.h`、`hal/hal*.h`、`support/<域>.h`。
- 后端实现与契约同名，一平台一文件夹。
- 三方库适配文件：`<库名>_port.c`。
- 私有/内部头：`_internal.h`/`_priv.h`，不上 include 路径。

---

## 4. 契约头规则

**只放**：函数声明、契约类型（含不透明句柄 `void *`）、错误码/常量/宏。
**禁止**：`.c` 实现、OS/平台头、`#ifdef PLATFORM`、对具体后端的依赖。
约定：句柄一律 `void *`；错误码 `0` 成功 / 负 errno；契约头必须能独立编译。

---

## 5. 后端规则（`backends/<axis>/<platform>/`）

1. **平台选择靠 CMake 文件集**，非源内 `#ifdef`。
2. **骨架铁律——不静默成功**：未实现返回 `-ENOSYS`/`NULL`；禁止空 `return 0`，禁止 `#error`。
3. **后端↔后端依赖只允许一条边**：`backends/hal/linux/hal_can.c` → `backends/osal/posix`（`osal_sem` 同步 + `osal_get_tick_ms`）。其余后端之间、后端与 `app/` 之间无横向依赖。
4. 新平台镜像主轴命名（如 `backends/osal/freertos`）。

> **换 RTOS 的真相**：换掉 Zephyr/Linux 会同时带走其 device 模型/DTS/驱动，因此"换 RTOS" = 换 osal 后端 **且通常**换 hal 后端（甚至补 `bsp`）。

---

## 6. include 策略

**固定搜索路径**（不随意新增）：

```
<root>                          # "osal/osal.h" "hal/hal_*.h" "support/*.h"
app/                            # "common/…" "bms/…" "sim/…"（应用模块，带前缀）
third_party/<lib>/…              # 三方件（各自 target_include_directories）
# 平台附加：backends/hal/<platform>/、cmake/zephyr_stubs（zephyr）
```

- ✅ 跨模块头带模块前缀；同模块私有头同目录裸引。
- ❌ 不把每个 `app/<mod>/inc` 加进路径；不依赖 `-Iapp/common` 解析裸名。
- 不做 `app/<mod>/{inc,src}` 拆分——除非某模块 >10 文件或确需隐藏内部头。

---

## 7. app / third_party 规则

- `app/`：顶层**扁平模块**（`.c` 与 `.h` 同目录）；`app/common/` 只放领域共享代码。
- `third_party/`：与 `app/` 并列；每三方件一个文件夹，vendored 原样 + `<lib>_port.c`；以 **STATIC 库**接入。

---

## 8. 来源与溯源（provenance）

- 本脚手架的 osal/hal 契约与后端移植自 `platform_framwork`；`support/` 为其新增的第三条轴。
- `backends/hal/linux/DEVIATIONS.md` 记录 Linux HAL 相对原始实现的偏差（3 编译 + 1 行为）。
- 本脚手架**即**该平台层的事实源（source of truth）；如需与上游再次同步，手动 diff 对应文件即可（无强制清单）。

---

## 9. 仓库卫生

- `.gitattributes` 强制 **LF**（Linux/WSL 构建，`*.sh` 出现 CRLF 会 `bash: $'\r': command not found`）。
- `.gitignore` 排除 `build*/`、`*.log`。
- 大重构前先 `git` 建基线提交。
- 构建脚本 `scripts/`、工具链 `cmake/toolchains/`、假 SDK 头 `cmake/zephyr_stubs/`。

---

## 10. 扩展流程

### 10.1 新增抽象轴
根目录建契约头目录 + `backends/<轴>/{…}`；契约只依赖 `osal/`、`hal/`、libc；双后端（真实 + 骨架）；加进 CMake 两分支；更新 §2.2 配对表。

### 10.2 新增平台后端
`backends/{osal,hal,support}/<新平台>/`；CMake 增 `PLATFORM` 分支 + 源文件集 + include 顺序；契约与 `app/` 零改动。

### 10.3 新增 HAL 外设
`hal/hal_<x>.h` 声明契约；`backends/hal/linux/hal_<x>.c`（真实）+ `backends/hal/zephyr/hal_<x>.c`（骨架）；两台 CMake 加入；zephyr 侧补 `cmake/zephyr_stubs/zephyr/…` 假头。

### 10.4 新增三方件
`third_party/<lib>/`（vendored 原样）+ 适配文件 `<lib>_port.c`（如需）；CMake 以 STATIC 库接入。

---

## 11. 构建与验证门禁

| 门禁 | 命令 | 通过标准 |
|---|---|---|
| Linux 构建 | `cmake -DPLATFORM=linux -S . -B build && cmake --build build -j` | 退出码 0 |
| Zephyr 编译检查 | `cmake -DPLATFORM=zephyr -S . -B build-zephyr && cmake --build build-zephyr -j` | 退出码 0（无 SDK，走 stubs） |
| ARM 交叉编译 | `./scripts/build-armhf.sh` | `file`/`readelf` 校验 ARM 硬浮点产物 |

**测试约定**：缺环境必须**显式 SKIP**，绝不静默判过。

---

## 12. 何时才需要 `bsp/` 或寄存器级 HAL

**仅在平台不提供 device 模型/板级描述时**（裸机 / 裸 FreeRTOS）才需要 `backends/hal/<chip>` + 简单 `bsp`。本项目两轴（linux\|zephyr）**永不**需要。自定义板的板级描述落在 **Zephyr board 模块**（`boards/<vendor>/…/*.dts`），不是本仓的 `bsp/`。

---

## 附录 A. 决策记录

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 后端目录名 | `backends/` | 最准确；`ports/` 需免责声明，`platform/` 与原则冲突 |
| 2 | 契约位置 | 根目录三轴 | 契约纯净、可整体拷贝；与后端一一配对 |
| 3 | 模块 `inc/src` 拆分 | 不做 | 小模块收益近零、易重名 |
| 4 | 跨模块头 | 模块前缀 | 消除多 `-I` 根的歧义 |
| 5 | 三方件位置 | `third_party/` | 隔离 vendored 与自有代码 |
| 6 | `bsp/` | 不做 | 板级描述交给 Zephyr DTS / Linux DT |
| 7 | 平台切换 | `-DPLATFORM` 选文件集 | 保持 app 零 `#ifdef` |

## 附录 B. 唯一后端→后端的边

```
app ─→ { osal 轴 ∥ hal 轴 ∥ support 轴 }
           │         │           │
      backends/osal/*  backends/hal/*  backends/support/*
                    └────┬────┘
          唯一垂直边: hal_linux → osal_posix
```
