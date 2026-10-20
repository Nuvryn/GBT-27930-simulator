# GB/T 27930 协议模拟器

通过 JSON 选择充电桩或 BMS，支持 2015 和 2023 A 类的握手、辨识、参数配置、准备、充电、中止、统计及异常恢复。每个进程模拟一个角色，通过 SocketCAN 对接。

日志使用 EasyLogger 异步缓冲和独立输出线程，默认保存到 `logs/年月日_时分秒_纳秒_进程号.log`。使用平台本地时间；`file_log=false` 可关闭文件保存。
`candump` 原始抓包由配置控制，默认关闭；Linux/ARM 按需设置 `"candump": true`，并部署 `can-utils` 的 candump。原始文件旁保存诊断文件。应用日志文件由 `file_log` 独立控制。

继电器、接触器、辅助电源、电子锁及采集操作仅记录模拟动作；条件判断与测量值来自配置和场景事件。运行平台为 ARM Linux，Zephyr 仅保留编译骨架。

依赖使用已经确认的 Open-SAE-J1939、cJSON、EasyLogger 固定源码；构建无需下载。新增库必须先征得使用者同意。来源、许可和哈希校验见 [依赖说明](docs/dependencies.md)。

## 构建与运行

在 Linux / WSL 根目录运行，需要 GCC、CMake、Ninja；测试另需 Python 3：

```bash
sudo apt-get install can-utils   # 仅启用抓包时需要；ARM 需匹配架构的 candump
cmake -G Ninja -DPLATFORM=linux -S . -B build
cmake --build build -j
./build/gbt27930_simulator --config config/charger_2015.json
# 在另一个终端启动对端，或仅启动一个角色对接实物：
./build/gbt27930_simulator --config config/bms_2015.json
```

默认配置使用 `can0`，2023 示例位于 `config/charger_2023.json`、`config/bms_2023.json`。`config/default.json` 是 2015 桩端。真实 CAN 的接口、250 kbit/s 速率和权限由系统预先配置；运行时不修改链路。默认使用真实时间，只有 `software_test=true` 才允许加速。

配置非法、CAN 或日志初始化失败、通信异常及会话耗尽均返回非零。正常统计完成返回 0。即使通信异常后重连成功，本轮测试仍保留失败结果，便于发现注入的异常。

## 验证

```bash
# 需要网络管理权限；准备 vcan 后运行全部 CTest：
sudo bash scripts/verify_simulator.sh --prepare-vcan
# 已有 vcan0 时也可普通用户运行：
SIM_SPEED=10 bash tests/test_runner.sh
bash scripts/verify_real_time.sh
# ARM 交叉编译、Zephyr 骨架、依赖哈希及 ASan/UBSan：
bash scripts/verify_build_matrix.sh
```

当前自动矩阵为 **29 个双进程场景**，另有独立字节向量、可控时间 TP/状态机测试、配置及启动失败测试；并非预先宣称的 51 场景。缺少 `vcan0` 返回 **77 / SKIP**，接口存在但通信失败属于 FAIL。
启用抓包的自动场景还要求 `candump` 可用，缺少工具时报告 SKIP；正式模拟器启用抓包但找不到工具则返回非零。

ARM 运行只需部署模拟器可执行文件与配置；启用抓包时额外提供 candump。无需 Python 或协议源码。Python 仅供开发测试使用。

报告写入 `build/scenarios/report.json`，每个场景保留双方配置与日志。真实时间回归另外保存 `report_real_time_2015.json` 和 `report_real_time_2023.json`，检查 TP 分包及周期。WSL 重启后需重新创建 vcan。

ARM 构建入口为 `bash scripts/build-armhf.sh`，产物为 `build-armhf/gbt27930_simulator`；默认导出各构建目录的 `compile_commands.json` 供 clangd 使用。`scaffold_demo` 继续保留为原平台验证示例。

## 模块与说明

| 路径 | 职责 |
|---|---|
| `app/config/` | 严格配置解析、默认值和交叉校验 |
| `app/logging/` | EasyLogger 封装，帧、状态、超时、模拟动作证据 |
| `app/transport/` | Open-SAE-J1939 非阻塞 TP 与接收前置校验 |
| `app/protocol/` | 报文元数据、字段范围、单位和显式字节编解码 |
| `app/simulator/` | 双角色、两版本流程及单线程运行调度 |
| `app/fault_injection/` | 有界延迟队列与确定性错误规则 |
| `osal/`、`hal/`、`support/` | 平台接口契约 |
| `backends/` | Linux 实现与 Zephyr 骨架 |
| `tests/` | 独立判据、自动场景与报告导出 |

详细用法见 [配置说明](docs/simulator_usage.md)、[字段表](docs/config_fields.md)、[基础接口](docs/protocol-core.md)、[验证清单与限制](docs/validation_matrix.md)、[分层约定](docs/architecture-spec.md)。

当前通过的是软件验证；真实桩和真实 BMS 联调尚缺设备，状态为 **SKIP**。2023 自动重连所引用的外部电气条件不由本项目判定，需要场景提供 `reconnect_allowed`；完整标准认证不在本次结果声明范围内。
