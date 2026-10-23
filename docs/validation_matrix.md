# 验证清单与交付边界

## LVGL UI 首版专项（2026-10-23）

固定 LVGL v9.6.0，依赖校验包含 1515 个上游文件。UI 关闭不链接 LVGL（符号检查），UI 开启要求 manual 模式；无 UI 构建却请求启用、设备初始化失败均非零退出。Linux 官方 fbdev 后端读取系统分辨率、格式和行跨度，evdev 读取输入坐标范围；本轮不开发或修改 BSP。

| 检查 | 结果 | 证据 |
|---|---|---|
| Linux 无 UI 完整回归（20 项） | PASS | build/Testing/Temporary/LastTest.log；含原有 29 个软件场景 |
| Linux UI 完整回归（22 项） | PASS | scripts/verify_ui.sh；界面新增用例及原协议基线 |
| 实际 LVGL 控件/指针输入 | PASS | sim_ui；800×480、480×272；build-ui/ui_*.ppm |
| 两版双角色重复、主动停止、Apply、暂停恢复及失败轨迹 | PASS | sim_ui；每组合两轮，核对状态/修订/参数及错误报文 |
| UI 持续刷新时的真实时间双进程 SocketCAN | PASS | ui_vcan；build-ui/ui_vcan_*/report.json；两版各两轮，配置证据完整，DT ≥10 ms，BCL/CCS 周期 50..100 ms |
| 有界控制、保存往返、精确数值和临时规则归属 | PASS | sim_control、sim_ui；另验证非法数组/非本端字段拒绝、旧会话停止拒绝、待机预置首帧注入 |
| 并发快照/日志和请求反馈 | PASS | control_concurrency；真实线程下 2000 个有序请求及结果 |
| ASan/UBSan | PASS | 无 UI 12 项、UI 5 项；后者包括实际 UI 线程和双进程对跑 |
| ARM Linux UI=OFF/ON | PASS | build-armhf、build-armhf-ui；ELF32 ARM EABI5，compile_commands.json 均导出 |
| Zephyr OFF 骨架及 ON 拒绝 | PASS | build-zephyr、build/zephyr_ui_rejected.log；不表示 Zephyr UI 移植完成 |
| 真实屏幕、触摸及旋转 | SKIP | 未连接显示及输入设备；离屏测试不替代设备验收 |
| ARM 板运行、真实桩/BMS 对接 | SKIP | 未连接实物 |

ui_vcan 使用正式入口及 UI/协议/日志线程，只在测试可执行文件中替换为内存显示，正式程序仍走系统显示设备。一次挂载盘日志轮询曾遇 WSL ENODATA，已仅对实时读取中的这一临时错误继续轮询；未延长协议超时或放宽时序、退出码、证据判据。人工查看了实际 LVGL 渲染图像，修正小屏流程卡片收缩问题。

操作及部署见 [UI 使用说明](ui_usage.md)。下方各专项为其对应阶段的历史验证记录，测试数量以本节为最新口径。

## Linux 手动重复会话专项（2026-10-23）

新增 `sim_session` 可控时间测试：两种版本各连续三轮、空闲等待超过会话时限仍不发送、不接受错误长度/对端 CHM、重复 start 拒绝、失败后计时重置及旧 CHM 连续流抑制。配置测试覆盖省略模式仍为 auto、manual 禁用自动重连、非法模式拒绝。

新增 `manual_sessions` 使用 WSL vcan 和真实子进程标准输入，验证两种版本各两轮正常充电、空闲期间无发送、Ctrl-C 中止后进程保留、连续失败后重新成功、EOF 和 SIGTERM 退出，并检查一轮 2015 真实时间手动充电。没有 vcan 返回 77。证据在 `build/manual_sessions_*/` 中的配置、应用日志和控制台文件。

本次 Linux CTest 共 17 项，原有自动软件场景仍为 29 个；ARM Linux 交叉编译、Zephyr 骨架和 9 项 ASan/UBSan 离线检查通过。新增能力未在 ARM 板或真实桩/BMS 上验证。

## Linux 启动信息专项（2026-10-23）

`simulator_cli` 验证 `--version` 在配置不存在时仍退出 0 且不创建文件；自动日志含有效配置和构建信息，控制台不泄漏详情；CAN 初始化失败不得输出启动完成。大配置覆盖命令行覆盖、默认值、全部 22 种报文设置、256 个单体及分组、128 个温度、32 条注入和 32 个事件，JSON 特殊字符及长字段分段仍可完整还原。

`sim_log` 对输出逐字节拆分，验证 `DETAIL` 不进入控制台、文件保留原文、普通及末尾不足标签长度的记录仍输出。`sim_core` 验证快照回调拒绝及多处分配失败传播，ASan/UBSan 检查资源释放。CTest 仍为 15 项，软件对跑仍为 29 个场景。

## 日志保留专项（2026-10-23）

`log_retention` 在隔离临时目录中验证：BMS/charger 各保留 3 次、应用/关联抓包/诊断成组清理、关闭应用日志后的独立抓包、进程间活动租约、异常退出释放、显式复用旧路径退出管理、未登记文件及非法标记保护、符号链接保护和管理初始化失败。`simulator_cli` 确认正式入口为自动日志生成归属标记。

保留策略固定为每角色 3 次，不限制单次文件大小；活动或明确退出管理的记录不计为可删除目标，旧版无归属标记文件不迁移、不自动删除。当前 CTest 共 15 项，软件场景仍为 29 个。

## 上海时间命名专项（2026-10-23）

自动生成的应用日志及独立 candump 文件名固定使用上海时间 UTC+8。`sim_log` 检查 UTC、UTC+8、带夏令时的其他时区和未设置 TZ 四种环境下的相同结果，覆盖跨年、闰日、缓冲区不足及时间溢出；不依赖系统时区数据库。

Linux 14 项 CTest（含 29 个双进程场景）、ARM 交叉编译、Zephyr 骨架及 7 项 ASan/UBSan 检查通过。首次加速回归的 candump_disabled、retry_recovers 曾出现 BRM TP 错误，完整复跑均通过；该偶发问题原因未确定，本次未修改协议时序或放宽测试判据。

验证日期：2026-10-20。环境：WSL Ubuntu 22.04、GCC 11.4、Python 3.10；CAN 软件联调使用 vcan0。所有结果对应源码及固定依赖，不代表第三方认证。

## 构建与基础验证

| 检查 | 结果 | 证据/复现入口 |
|---|---|---|
| Linux 严格告警构建与 17 项 CTest | PASS | `scripts/verify_simulator.sh --prepare-vcan`；`build/Testing/Temporary/LastTest.log` |
| 2015、2023 双角色正常充电及双方主动结束 | PASS | 四个 normal 场景及独立可控时间引擎测试 |
| 真实时间双角色正常充电 | PASS | `scripts/verify_real_time.sh`；`report_real_time_2015.json` / `report_real_time_2023.json` |
| DT 最小 10 ms 与 BCL/CCS 50 ms 调度 | PASS | 独立 TP 可控时间检查、真实时间日志间隔；后者允许至 100 ms 用于本机回归 |
| ASan + UBSan | PASS | `scripts/verify_build_matrix.sh` 的 9 项会话/保留策略/抓包/日志/核心/启停边界/CLI 检查；启动、异常及最大 TP 负载路径 |
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

`message_lifecycle` 使用独立脚本化对端，检查两版报文启停边界：BRO 实际发送 AA 与接收 CRO AA 的双条件、CCS 启动差异、BST 回复次数、CST 等待 BSD、CSD 不提前启动、统计报文保持至 CRM/模拟断电、错误报文跨恢复保持，以及 2023 两次更新 BCP 完成后才发送 BRO 00。四个正常双进程场景还根据原始帧日志检查关键先后关系与 BST 次数。

## 软件场景矩阵

每个场景启动两个独立进程。PASS 表示双方退出码和外部证据符合预期，异常场景允许且通常要求进程退出 1。29 个场景没有覆盖所有“版本 × 角色 × 阶段 × 正常/异常”的笛卡尔积，不能据此声称所有协议场景已完成。

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
| candump_disabled | 2015 | 双方/全流程 | 关闭抓包不启动外部工具、不创建抓包文件 |
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
## 异步日志专项回归

- sim_log：先填满无读取者的 stdout 管道，确认日志生产者仍能返回；随后启动读取者，退出时排空并 join。
- 锁住消费者后填充异步缓冲，验证 ENOSPC 可见、关闭返回失败；再次打开无残留并保留最后一条日志。
- 使用文件大小限制制造工作线程写入失败，验证排空/关闭将 EIO 返回给调用方。
- simulator_cli：未指定 log_path 时分别启动 BMS 和桩端，验证上海时间秒级加角色名、不含 PID、双方文件不重名及 CAN 初始化失败日志已在退出前落入文件。
- 29 个双进程场景及两版真实时间回归继续检查最终证据，覆盖文件启用/关闭。

Zephyr 的异步工作线程与文件后端当前仍返回 ENOSYS，骨架构建不代表具备日志线程运行能力。

## 可选 candump 抓包

29 个双进程场景中的 28 个通过测试配置显式启用抓包，核对每个角色实际成功 TX 的 ID、数据及次数均出现在对应原始文件中；另有关闭抓包的独立场景。产品默认关闭抓包。两版真实时间正常场景同样检查原始帧覆盖。

can_capture 使用独立替身进程检查：工具缺失、非法接口参数、启动后退出、stderr 诊断、已有文件保护、正常停止、停止超时强制回收、丢帧诊断、文件写入失败及无残留子进程。上述替身测试不替代使用真实 candump 的 vcan 回归。
