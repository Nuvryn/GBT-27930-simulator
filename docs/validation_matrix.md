# 验证清单与交付边界

验证日期：2026-10-20。环境：WSL Ubuntu 22.04、GCC 11.4、Python 3.10；CAN 软件联调使用 vcan0。所有结果对应源码及固定依赖，不代表第三方认证。

## 构建与基础验证

| 检查 | 结果 | 证据/复现入口 |
|---|---|---|
| Linux 严格告警构建与 11 项 CTest | PASS | `scripts/verify_simulator.sh --prepare-vcan`；`build/Testing/Temporary/LastTest.log` |
| 2015、2023 双角色正常充电及双方主动结束 | PASS | 四个 normal 场景及独立可控时间引擎测试 |
| 真实时间双角色正常充电 | PASS | `scripts/verify_real_time.sh`；`report_real_time_2015.json` / `report_real_time_2023.json` |
| DT 最小 10 ms 与 BCL/CCS 50 ms 调度 | PASS | 独立 TP 可控时间检查、真实时间日志间隔；后者允许至 100 ms 用于本机回归 |
| ASan + UBSan | PASS | `scripts/verify_build_matrix.sh` 的 4 项核心/CLI 检查；启动、异常及最大 TP 负载路径 |
| 固定依赖完整性 | PASS | `scripts/check_dependencies.py`，原始 Open-SAE-J1939 58 文件、cJSON、EasyLogger |
| ARM Linux 交叉编译 | PASS | `build-armhf/gbt27930_simulator`；ELF32 ARM EABI5，v7，VFP 寄存器传参 |
| Zephyr 骨架编译 | PASS | `build-zephyr/gbt27930_simulator`；仅桩头文件构建，无真实 SDK/设备运行 |
| 编译数据库 | PASS | Linux、ARM、Zephyr 构建目录各自导出 compile_commands.json |
| 缺少 vcan0 的判据 | PASS | `sudo bash scripts/verify_skip.sh`；隔离网络输出 SKIP，退出 77 |
| ARM 板运行 | SKIP | 未连接 ARM 板，交叉编译不能替代运行验证 |
| 模拟桩对真实 BMS | SKIP | 未连接设备 |
| 模拟 BMS 对真实桩 | SKIP | 未连接设备 |

CTest 中 `sim_core` 验证独立 BCL、BCP、CTS、BMV 字节向量及两版长度；其余字段通过注册表范围和长度检查及编码/解码往返覆盖，尚需补充更多独立人工向量。最大 1785 字节 TP、序号 0、短帧、RTS 冲突、提前 ACK、实际发送失败、T1～T4 及回绕有独立测试。

`sim_engine` 对 2015 的 BRM、BCP、CML、BRO、CRO、BCL、BCS、CCS、BSM、BST、CST、BSD、CSD 分别丢弃，确认接收方在有限时间内失败；另覆盖 2023 从未收到 BSM 时的特定规则。测试时间可控，不靠 sleep 判断超时边界。

## 软件场景矩阵

每个场景启动两个独立进程。PASS 表示双方退出码和外部证据符合预期，异常场景允许且通常要求进程退出 1。28 个场景没有覆盖所有“版本 × 角色 × 阶段 × 正常/异常”的笛卡尔积，不能据此声称所有协议场景已完成。

| 场景 | 版本 | 主要角色/阶段 | 类型与判据 |
|---|---|---|---|
| normal_2015_charger_stop | 2015 | 双方/全流程 | 桩主动结束，双方统计完成 |
| normal_2015_bms_stop | 2015 | 双方/全流程 | BMS 主动结束，双方统计完成 |
| normal_2023_charger_stop | 2023 | 双方/全流程 | 桩主动结束，CCS 7 字节 |
| normal_2023_bms_stop | 2023 | 双方/全流程 | BMS 主动结束，统计顺序 |
| optional_long_payloads | 2015 | BMS/充电 | BMV、BMT、BSP 均实际发送 |
| bcl_missing | 2015 | 桩/充电 | BCL 持续接收超时 |
| bcl_invalid_mode | 2015 | 桩/充电 | 非法字段拒绝且不刷新计时 |
| tp_sequence_zero | 2015 | 双方/辨识 | 非法 DT 序号导致有限时间失败 |
| not_ready | 2015 | 双方/准备 | 未就绪结果导致准备超时 |
| selftest_failure | 2015 | 桩/辨识 | 配置自检失败 |
| precharge_failure | 2015 | 桩/准备 | 配置预充失败 |
| bcl_duplicate | 2015 | BMS/充电 | 精确命中 3 次，最终正常 |
| bcl_delay | 2015 | BMS/充电 | 精确命中 2 次，最终正常 |
| bsm_absent_2023 | 2023 | 桩/充电 | 从未收到 BSM，不启动其超时 |
| bsm_absent_2015 | 2015 | 桩/充电 | 首次 BSM 等待超时 |
| bsm_lost_2023 | 2023 | 桩/充电 | 收到后丢失 BSM，禁止自动重连 |
| pause_resume_2023 | 2023 | 双方/充电 | 暂停及恢复动作证据 |
| foreign_bcl_id | 2015 | 桩/充电 | 外部源地址不刷新计时 |
| short_bcl | 2015 | 桩/充电 | 非法长度被拒绝 |
| parameter_event | 2015 | BMS/充电 | 电流事件变化后的独立字节证据 |
| version_length_mismatch | 混合 | BMS/充电 | 2015 CCS 不被 2023 长度规则接受 |
| soc_target_stop | 2015 | BMS/充电 | 场景 SOC 达标主动结束 |
| rx_drop_rule | 2015 | 桩/充电 | 接收方向注入导致超时 |
| bounded_drop_window | 2015 | BMS/充电 | 限时丢弃后恢复正常发送 |
| no_file_logging | 2015 | 双方/全流程 | 关闭后不创建日志文件，控制台可判定 |
| retry_recovers | 2015 | 双方/恢复 | 有限次数丢包后重新辨识并完成统计，保留失败历史 |
| retry_exhausted | 2015 | 双方/恢复 | 有限重试耗尽，无无限等待 |
| unplug_replug | 2023 | 双方/充电与重启 | 模拟断开、重新插拔后重新握手 |

当前完整报告为 `build/scenarios/report.json`；每个目录下保留精确配置、双方帧日志和退出证据。构建及报告产物被 Git 忽略，复现脚本和判据纳入版本控制。

## 版本差异和资料边界

本地整理文档为实现依据。2015/2023 共用明确一致的字段；差异分支包括 CCS 长度、2023 BSM 首次接收条件、准备计时起点、低需求电压中止、暂停 10 分钟限制、BSM 丢失后的重连锁定及统计发送顺序。低需求电压和暂停极限等分支尚需扩充外部独立场景，当前不列为已独立验证。

BRM 使用资料勘误后的 49 字节；资料称 23 种报文，但逐项仅列出 22 种，实现没有补造第 23 种。CTS 日期时间由配置的 BCD 字段提供，不调用墙上时钟。

2023 引用 GB/T 18487.1 的电气连接、安全和自动重连条件未在本地资料完整定义。实现采用明确的 `reconnect_allowed` 场景条件，默认 false；日志模拟动作不提供该外部条件的合规结论。后续需拿到规范条文及真实设备核对。2023 B/C 类、BAM、自动地址声明、完整 J1939 应用层不在范围内。

## 实物联调步骤

1. 在目标 Linux 上准备 SocketCAN 驱动、接口及权限，按设备要求配置 250 kbit/s；确认 CAN 地址、物理接线和终端电阻。
2. 从对应角色/版本示例复制配置，设置真实接口和设备参数，保持 `software_test=false`、`speed=1`，启用不同路径的文件日志。
3. 一次仅启动一个模拟角色，让真实设备作为对端；外部硬件操作由试验环境提供，程序仅打印模拟动作。
4. 收集模拟器日志、进程退出码、独立总线抓包及设备侧结果，逐阶段人工比对字段、周期、终止原因与恢复条件。
5. 缺少设备或无法满足外部电气条件记 SKIP；协议证据不符记 FAIL；不得用双方软件互通或编译通过代替实物 PASS。

实物记录至少包含版本、角色、设备标识、配置摘要、阶段、预期响应、实际响应、PASS/FAIL/SKIP 及证据路径。硬件缺席不会阻止软件侧开发，但实物交付验收仍未完成。