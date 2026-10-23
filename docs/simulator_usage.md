# 模拟器配置与场景

## 启动与生命周期

`gbt27930_simulator --config <JSON>`；未指定路径时加载 `config/default.json`。`--speed 1..100` 可覆盖配置，只在 `software_test=true` 时允许大于 1。一个进程拥有一个角色、一个 CAN 接口及一个对端，协议线程统一调度，CAN 后端只接收并缓存原始帧。

配置加载、日志打开、CAN 打开、引擎初始化任一步失败退出 1；资源按相反顺序释放。日志写入失败、CAN 接收环溢出及实际发送失败均可见并终止。SIGINT/SIGTERM 请求正常中止流程，无法完成通信则以会话时限结束。

### Linux 版本查询与启动信息

`gbt27930_simulator --version` 打印软件版本、构建时间及目标平台/架构后退出 0，不读取配置、不打开 CAN、不创建日志。软件版本来自 `CMakeLists.txt` 的项目版本，当前为 `0.1.0`，与 2015/2023 协议版本分开。不包含 Git 提交号或 Debug/Release 信息。

```text
GB/T 27930 Simulator v0.1.0
Build: 2026-10-23T10:30:45Z
Target: linux/arm
```

构建信息由 CMake 在每次构建时生成并嵌入程序，运行目标无需部署 Git 或版本文件。`Build` 使用明确标注的 UTC（末尾 Z），不受构建机本地时区影响；如设置 `SOURCE_DATE_EPOCH`，按该环境变量生成可复现时间。日志文件名仍使用上海时间 UTC+8。编译器名称和版本记录到文件。

正常启动的新增信息全部使用英文：控制台显示版本、构建时间、目标、配置来源、角色/协议/CAN 摘要、日志及抓包路径；资源初始化成功后输出 `READY ... Startup complete`。配置解析失败时也会先显示版本和配置路径，但不会报告启动完成。原有运行期协议报文及状态日志的控制台策略保持不变。

文件另外保存 `DETAIL` 记录：软件/构建/编译器信息、配置来源及 `speed_override`，以及 `CONFIG_BEGIN`、编号的 `CONFIG part=N data=...` 和 `CONFIG_END`。按编号直接拼接每段 data 可还原有效配置 JSON；内容包含默认值、命令行覆盖后的值、全部物理参数、周期/首次及持续超时、数组、注入规则和场景事件。不是简单复制原始配置文件。

完整配置经 cJSON 转义并分段，通过同一个 EasyLogger 异步线程写入文件，不在控制台重复输出。启动期间允许等待日志缓冲容量，最多等待 5 秒；分配、格式化或输出失败会令启动返回非零。`file_log=false` 时仍显示摘要，但不保存完整配置。该等待只发生在协议调度启动前。当前只实现 Linux 查询与分流，Zephyr 继续保留骨架。

## 根配置项

所有时间单位为协议毫秒；真实设备使用 `speed=1`。布尔值必须是 JSON 布尔值，整数不接受小数、溢出或无法按编码分辨率表示的数值。未知键、重复键、非法版本、相同源/目的地址均拒绝。文件不超过 64 KiB，嵌套深度不超过 32；不接受嵌入 NUL。

| 配置键 | 默认值 | 含义及范围 |
|---|---|---|
| `role` | 必填 | `charger` / `bms` |
| `version` | 必填 | 2015 / 2023；2023 仅 A 类 |
| `can_interface` | `can0` | 接口名，最多 15 字节 |
| `source` / `peer` | 桩 86/244；BMS 244/86 | 0..253，不能相同 |
| `software_test` / `speed` | false / 1 | 加速仅用于双方受控软件回归，速度 1..100 |
| `file_log` / `log_path` | true / 空 | 默认生成 logs/YYYYMMDD_HHMMSS_role.log，固定使用上海时间（UTC+8）；显式路径最多 255 字节并保留覆盖行为；false 关闭文件保存 |
| `candump` / `candump_path` | false / 空 | 显式启用后启动外部 candump；空路径关联应用日志，文件日志关闭时自行按真实时间命名；显式路径最多 255 字节，已有文件拒绝覆盖 |
| `self_test_pass` | true | 自检结果；false 进入故障处理 |
| `parameters_match` | true | 场景给出的参数匹配条件，同时仍校验收到的电压与能力范围 |
| `precharge_pass` | true | 预充结果；false 不进入正常充电 |
| `ready` | true | 就绪结果，false 保持未就绪，受准备超时约束 |
| `ready_delay_ms` | 250 | 0..3600000；模拟准备耗时 |
| `ready_timeout_ms` | 60000 | 1..3600000；整体准备等待上限 |
| `stop_after_ms` | 5000 | 0..3600000；进入充电后的主动结束时间，0 禁用；BMS 示例为 0 |
| `auto_aux_power_off` | true | 统计完成后按场景自动关闭模拟辅助电源，仅记录动作，不操作硬件 |
| `aux_power_off_delay_ms` | 1000 | 0..3600000；统计完成至上述模拟断电的等待时间 |
| `max_session_ms` | 180000 | 1..3600000；整个进程会话上限 |
| `recovery_wait_ms` | 30000 | 1..3600000；恢复等待上限 |
| `retries` | 3 | 0..100；恢复尝试数，0 不重试 |
| `reconnect_allowed` | 2015 true；2023 false | 外部场景是否满足重连条件，不模拟 GB/T 18487.1 电气检测 |
| `dt_interval_ms` | 10 | 1..1000；DT 包间隔下限；小于 10 仅用于故意制造时序偏差 |
| `tp_t1_ms` / `tp_t2_ms` | 750 / 1250 | 1..3600000；接收下一 DT / CTS 后首 DT 等待 |
| `tp_t3_ms` / `tp_t4_ms` | 1250 / 1050 | 1..3600000；CTS/结束应答等待 / CTS Hold 等待 |
| `tp_rx_window` | 16 | 1..255；同时受发送端 RTS 最大窗口限制 |

`parameters` 对象中的物理量见 [完整字段表](config_fields.md)。例如：

```json
{
  "role": "bms",
  "version": 2015,
  "can_interface": "vcan0",
  "stop_after_ms": 0,
  "file_log": true,
  "log_path": "bms.log",
  "parameters": {
    "bcl_voltage_mv": 420000,
    "bcl_current_ma": 100000,
    "bcl_mode": 2
  },
  "messages": {
    "BCS": {"period_ms": 250},
    "CCS": {"first_timeout_ms": 1000, "timeout_ms": 1000},
    "BMV": {"enabled": true, "period_ms": 10000}
  },
  "cell_voltage_mv": [3500, 3510],
  "cell_group": [0, 0]
}
```

`messages` 键为大写报文简称，支持 `enabled`、`period_ms`、`first_timeout_ms`、`timeout_ms`。周期与超时范围 1..3600000。首次等待与后续持续接收分开计时；合法且处于期望阶段的完整负载才刷新计时。禁用本端发送并不会禁用对端对该报文的超时检查。默认禁用 BMV/BMT/BSP，其他报文按当前阶段及角色选择发送。

各报文默认周期/超时见 `app/protocol/gbt27930_codec.c` 描述符，首次等待默认等于持续接收超时。报文长度、单位、无效值见本地协议资料和 [基础说明](protocol-core.md)。

其他根字段：`manufacturer` 固定 4 字节、`vin` 固定 17 字节、`region` 固定 3 字节、`software_bytes` 8 个字节、`reserved_bytes` 16 个字节；字节范围 0..255。`cell_voltage_mv` 为 1..256 个 0..24000 mV 的 10 mV 整数倍；`cell_group` 为同长度 0..15 分组号，省略为 0；`temperature_degc` 为 1..128 个 −50..200 ℃整数。

统计完成后的发送仍遵循各版本停止条件。2015 BMS 收到 CSD 后继续 BSD，直到新 CRM 或模拟断电；2023 BMS 收到 CSD 后结束。桩端 CSD 在收到 BSD 后才启动，保持到模拟断电。默认等待 1000 ms 自动模拟断电；将 `auto_aux_power_off` 设为 false 时，可在 `done` 阶段配置 `aux_power_off` 事件，未提供条件则仍受 `max_session_ms` 限制。

2023 BMS 首次收到 CML 后，可在实际发送 BRO AA 前通过事件修改 BCP 字段。下面配置会先完成两次更新 BCP 传输，再发送 BRO 00 并等待就绪；桩端在准备阶段重新校验收到的更新参数：

```json
{
  "role": "bms",
  "version": 2023,
  "can_interface": "vcan0",
  "auto_aux_power_off": false,
  "events": [
    {"stage": "ready", "action": "set_parameter", "parameter": "bcp_max_voltage_mv", "value": 440000},
    {"stage": "done", "after_ms": 1500, "action": "aux_power_off"}
  ]
}
```

## 错误注入

最多 32 条 `faults`，与正常物理参数分开；同一次处理只命中声明顺序中的第一条合格规则。完整应用负载和底层 TP 帧分别经过注入点。延迟和重复产生的数据直接派发，不再次递归命中规则。

```json
{
  "stage": "charging",
  "message": "BCL",
  "direction": "tx",
  "action": "drop",
  "after_ms": 200,
  "duration_ms": 500,
  "count": 0
}
```

| 键 | 规则 |
|---|---|
| `stage` | handshake / identify / config / ready / charging / stopping / statistics / recovery / done / `*` |
| `message` | 大写应用报文简称，或 `TP_CM` / `TP_DT` |
| `direction` | tx 默认；rx 在接收处理前注入 |
| `action` | drop 丢弃；delay 延迟；duplicate 重复；corrupt 修改字节；length 修改有效长度；id 修改 CAN 标识符 |
| `after_ms` / `duration_ms` | 默认 0；起点相对当前阶段；`*` 相对启动；duration=0 不限时，其他值窗口右端不包含 |
| `count` | 默认 1；0 不限次数，1..1000000 限制命中次数 |
| `delay_ms` | 默认 100，1..3600000，用于 delay 和 duplicate |
| `byte` / `value` | corrupt 的字节偏移 0..1784 与新值 0..255；length 的值 0..8；id 的值为 29 位扩展标识符 |

制造 TP 长度错误：选择 `TP_CM`，corrupt 的 byte=1/2 修改 RTS 总字节数，byte=3 修改包数；序号错误选择 `TP_DT`、byte=0。延迟队列容量 64，队列满明确失败。日志包含阶段、方向、命中次数和修改前后值。规则次数跨重连保留，挂起延迟帧在重连时取消。

## 自动事件

最多 32 条 `events`，每条一次触发。阶段和时间规则与 faults 相同。`action` 为 stop、pause、resume、unplug、replug、aux_power_off、set_parameter；set_parameter 还需合法的 `parameter` 和 `value`。`aux_power_off` 提供模拟辅助电源关闭条件，只记录动作并停止本端通信。示例：

```json
[
  {"stage":"charging", "after_ms":300, "action":"pause"},
  {"stage":"charging", "after_ms":700, "action":"resume"},
  {"stage":"charging", "after_ms":900, "action":"set_parameter", "parameter":"bcl_current_ma", "value":50000}
]
```

事件中的参数名引用同一字段表，不允许用非法正常参数代替错误注入。暂停通过 BSM/CCS 允许位表达；2023 桩端暂停持续 10 分钟后结束。拔枪后停止通信，重插清除连接禁止状态并重新握手。2023 收到过 BSM 后再丢失会锁定自动重连；此前从未收到 BSM 则不启动该超时。

## 日志与判定

帧日志包含协议时间、方向、29 位 ID、DLC 和全部字节；MESSAGE、STATE、TP、ERROR、FAULT、EVENT、ACTION 记录其上下文。ACTION 全部是模拟，不访问硬件。CCS 等采集值来自配置、请求钳位后的模拟输出或事件，不反读 ADC。

采用 EasyLogger 原生异步缓冲，由独立线程批量输出。64 KiB 缓冲预先分配，协议线程不等待磁盘/终端 I/O；缓冲满会拒绝整条日志并锁存 ENOSPC，最终退出非零。写入失败同样锁存。退出先停止生产、排空并 join 线程，再关闭文件；日志保证到达 stdio 刷新层，不等于断电持久化。自动生成的应用日志和独立 candump 文件名固定使用上海时间（UTC+8），不受 TZ、系统时区或时区数据库缺失影响，也不修改进程时区。系统为 UTC 或上海时区时均只转换一次；日期跨天、跨月、跨年自动处理。文件名时间精确到秒，不包含纳秒；使用 bms/charger 角色名区分双方，不再包含 PID；同一角色在同一秒重复启动时拒绝覆盖已有文件并返回失败。系统时钟仍需准确；显式指定的文件路径保持原样。

正常完成输出 `RESULT status=PASS` 并退出 0；任何一次协议故障会保留本次进程的失败结果，即使后来成功重连并完成统计。异步写入在最后排空时失败也会令进程退出非零，工作线程另输出带错误码的 FAIL 诊断。场景报告的 PASS 表示观察到了预期结果，因此有意丢包的场景可以在模拟器退出 1 时判为 PASS。

### 自动保留最近三次运行

Linux 默认按角色分别保留最近 **3 次**自动日志记录，包含当前运行；按上海时间文件名从新到旧排序，BMS 与 charger 分开计数。新应用日志创建成功后执行清理；自动抓包启动成功后加入同一组。一次运行的应用日志、关联 candump 原始文件及 stderr 诊断一起保留、一起删除。关闭应用文件日志时，自动命名的独立抓包也可单独组成一组。

`logs/.<时间>_<角色>.session` 是程序的归属标记，`.retention.lock` 用于启动阶段串行清理。只有名称和标记内容均合法的记录参与清理。旧版无标记日志、手工文件、符号链接和用户显式设置的路径不自动删除；如果将以前管理过的应用日志改为显式路径，其整个旧组会退出自动管理。标记及锁文件不应手工移动或修改，正常运行不需要额外脚本或库。

正在运行的会话及正在写入的文件通过锁保护，必要时可暂时超过 3 次；退出后在下次启动时清理。异常进程退出会由内核释放锁，不会留下永久“运行中”状态。管理目录不跟随符号链接，清理不递归、不涉及其他目录。目录扫描及删除均在初始化阶段完成，不进入协议调度循环；清理发生 I/O 错误时记录错误并返回非零。该策略限制运行次数，不限制单次运行大小或总容量。

默认上海时间和秒级角色命名不变，自动保留 3 次不需要新增配置项：

```json
{
  "role": "bms",
  "version": 2015,
  "can_interface": "can0",
  "file_log": true,
  "candump": true
}
```

显式设置 `log_path` 时应用日志及其派生抓包不纳入管理；单独设置 `candump_path` 时该抓包及其诊断不纳入管理，自动应用日志仍按 3 次保留。CAN 初始化失败的运行如果已创建自动应用日志，同样计入一次。

### 可选原始抓包

Linux 使用 `posix_spawnp` 直接启动 `candump -L -d <接口>,0:0,#FFFFFFFF`，不通过 shell。捕获同一接口上的全部数据帧及错误帧，不限于当前角色。底层抓到的是实际总线数据，包括错误注入后的帧；软件丢弃而未发送的报文不会出现在总线上。外部工具来源为 [linux-can/can-utils](https://github.com/linux-can/can-utils)，部署时需将匹配目标架构的 `candump` 放入 PATH。WSL 验证使用发行版 can-utils 2020.11.0-1。

默认文件名为 `<应用日志路径>.candump.log`，例如 `logs/20261020_183000_bms.log.candump.log`；如果 `file_log=false`，抓包仍可独立开启，生成 `logs/YYYYMMDD_HHMMSS_role.candump.log`。诊断另存 `<抓包路径>.stderr.log`。二者均独占创建，避免覆盖旧证据；应用日志中 CAPTURE 条目记录实际路径。

抓包由外部进程接收，平台工作线程保存 stdout/stderr，不在协议线程中写抓包文件。原始格式包含真实 Unix 时间戳、接口、CAN ID、数据，不随 speed 改变时间戳；加速回归的真实帧间隔会相应缩短。

启动前保留 100 ms 真实时间用于观察早期错误；结束后保留 100 ms 让工具接收最后的内核回环帧，再发送 SIGINT、排空管道、等待子进程退出。工具没有就绪握手，这些宽限不能证明任意负载下零丢帧。退出不响应时升级 SIGKILL 并报告失败。缺少工具、提前退出、stderr/丢帧诊断、写文件失败或停止超时均使本次进程退出非零；关闭 candump 不影响普通协议运行。SIGKILL 杀死模拟器或系统掉电不保证抓包子进程清理和日志完整性。

默认和示例配置均关闭抓包，不依赖 candump。Linux 按需配置：

```json
{"role":"charger", "version":2015, "can_interface":"can0", "candump":true}
```

未来 MCU/Zephyr 移植可保持 `candump=false`，并按需设置 `file_log=false`；不要求实现文件保存。当前 Zephyr 仍只是骨架：外部抓包启动返回 ENOSYS，日志线程也尚未完成移植。若显式启用不支持的功能，应明确失败。

自动场景显式启用抓包并核对成功发送帧的 ID/字节/次数，包含最后的统计帧；另保留关闭抓包的用例。目标端无需 Python 或协议源码，抓包仅保存 candump 原始格式及诊断信息。

自动运行器独立检查双方退出码、日志证据和正常状态顺序；不读取引擎内部状态。输出包含 PASS/FAIL/SKIP、错误说明、退出码和证据目录。支持 `--case <名称>`、`--build-dir <路径>`、`--real-time`。真实时间正常用例检查 DT 至少 10 ms、BCL/CCS 周期 50..100 ms；100 ms 上界用于暴露本机调度迟滞，不作为标准允许偏差。
