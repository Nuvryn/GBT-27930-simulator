# 模拟器配置与场景

## 启动与生命周期

`gbt27930_simulator --config <JSON>`；未指定路径时加载 `config/default.json`。`--speed 1..100` 可覆盖配置，只在 `software_test=true` 时允许大于 1。一个进程拥有一个角色、一个 CAN 接口及一个对端，协议线程统一调度，CAN 后端只接收并缓存原始帧。

配置加载、日志打开、CAN 打开、引擎初始化任一步失败退出 1；资源按相反顺序释放。日志写入失败、CAN 接收环溢出及实际发送失败均可见并终止。SIGINT/SIGTERM 请求正常中止流程，无法完成通信则以会话时限结束。

## 根配置项

所有时间单位为协议毫秒；真实设备使用 `speed=1`。布尔值必须是 JSON 布尔值，整数不接受小数、溢出或无法按编码分辨率表示的数值。未知键、重复键、非法版本、相同源/目的地址均拒绝。文件不超过 64 KiB，嵌套深度不超过 32；不接受嵌入 NUL。

| 配置键 | 默认值 | 含义及范围 |
|---|---|---|
| `role` | 必填 | `charger` / `bms` |
| `version` | 必填 | 2015 / 2023；2023 仅 A 类 |
| `can_interface` | `can0` | 接口名，最多 15 字节 |
| `source` / `peer` | 桩 86/244；BMS 244/86 | 0..253，不能相同 |
| `software_test` / `speed` | false / 1 | 加速仅用于双方受控软件回归，速度 1..100 |
| `file_log` / `log_path` | true / 空 | 默认生成 logs/YYYYMMDD_HHMMSS_纳秒_PID.log，使用平台本地时间；显式路径最多 255 字节并保留覆盖行为；false 关闭文件保存 |
| `candump` / `candump_path` | true / 空 | 同步启动外部 candump；空路径默认关联应用日志，文件保存关闭时自行按真实时间命名；显式路径最多 255 字节，已有文件拒绝覆盖 |
| `self_test_pass` | true | 自检结果；false 进入故障处理 |
| `parameters_match` | true | 场景给出的参数匹配条件，同时仍校验收到的电压与能力范围 |
| `precharge_pass` | true | 预充结果；false 不进入正常充电 |
| `ready` | true | 就绪结果，false 保持未就绪，受准备超时约束 |
| `ready_delay_ms` | 250 | 0..3600000；模拟准备耗时 |
| `ready_timeout_ms` | 60000 | 1..3600000；整体准备等待上限 |
| `stop_after_ms` | 5000 | 0..3600000；进入充电后的主动结束时间，0 禁用；BMS 示例为 0 |
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

最多 32 条 `events`，每条一次触发。阶段和时间规则与 faults 相同。`action` 为 stop、pause、resume、unplug、replug、set_parameter；set_parameter 还需合法的 `parameter` 和 `value`。示例：

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

采用 EasyLogger 原生异步缓冲，由独立线程批量输出。64 KiB 缓冲预先分配，协议线程不等待磁盘/终端 I/O；缓冲满会拒绝整条日志并锁存 ENOSPC，最终退出非零。写入失败同样锁存。退出先停止生产、排空并 join 线程，再关闭文件；日志保证到达 stdio 刷新层，不等于断电持久化。生成文件名受 Linux TZ/系统时区影响，可使用 `TZ=Asia/Shanghai` 指定中国时区。

正常完成输出 `RESULT status=PASS` 并退出 0；任何一次协议故障会保留本次进程的失败结果，即使后来成功重连并完成统计。异步写入在最后排空时失败也会令进程退出非零，工作线程另输出带错误码的 FAIL 诊断。场景报告的 PASS 表示观察到了预期结果，因此有意丢包的场景可以在模拟器退出 1 时判为 PASS。

### 原始抓包与易读视图

Linux 使用 `posix_spawnp` 直接启动 `candump -L -d <接口>,0:0,#FFFFFFFF`，不通过 shell。捕获同一接口上的全部数据帧及错误帧，不限于当前角色。底层抓到的是实际总线数据，包括错误注入后的帧；软件丢弃而未发送的报文不会出现在总线上。外部工具来源为 [linux-can/can-utils](https://github.com/linux-can/can-utils)，部署时需将匹配目标架构的 `candump` 放入 PATH。WSL 验证使用发行版 can-utils 2020.11.0-1。

默认文件名为 `<应用日志路径>.candump.log`，例如 `logs/20261020_183000_123456789_4321.log.candump.log`；如果 `file_log=false`，抓包仍可独立开启，生成 `logs/YYYYMMDD_HHMMSS_纳秒_PID.candump.log`。诊断另存 `<抓包路径>.stderr.log`。二者均独占创建，避免覆盖旧证据；应用日志中 CAPTURE 条目记录实际路径。

抓包由外部进程接收，平台工作线程保存 stdout/stderr，不在协议线程中写抓包文件。原始格式包含真实 Unix 时间戳、接口、CAN ID、数据，不随 speed 改变时间戳；加速回归的真实帧间隔会相应缩短。

启动前保留 100 ms 真实时间用于观察早期错误；结束后保留 100 ms 让工具接收最后的内核回环帧，再发送 SIGINT、排空管道、等待子进程退出。工具没有就绪握手，这些宽限不能证明任意负载下零丢帧。退出不响应时升级 SIGKILL 并报告失败。缺少工具、提前退出、stderr/丢帧诊断、写文件失败或停止超时均使本次进程退出非零；关闭 candump 不影响普通协议运行。SIGKILL 杀死模拟器或系统掉电不保证抓包子进程清理和日志完整性。

易读视图使用 `python3 scripts/read_can_capture.py <原始文件>` 离线生成。字段位置与单位读取工程现有 codec 注册表，额外展示 TP.CM 类型和 TP.DT 包序号；不重写传输栈、不重组 TP 负载、不代替一致性判定。自定义源地址时使用 `--charger-address 0x56 --bms-address 0xF4`。原始未知行、标准帧和错误帧保留供人工核对。模拟器在 ARM 上运行无需 Python，转换视图可以在开发 PC 上执行。

自动场景为每个角色生成原始文件、HTML、文本视图，并将成功发送帧与 candump 内容按 ID/字节/次数核对，包含最后的统计帧。报告中的 readable_captures 指向易读 HTML。

自动运行器独立检查双方退出码、日志证据和正常状态顺序；不读取引擎内部状态。输出包含 PASS/FAIL/SKIP、错误说明、退出码和证据目录。支持 `--case <名称>`、`--build-dir <路径>`、`--real-time`。真实时间正常用例检查 DT 至少 10 ms、BCL/CCS 周期 50..100 ms；100 ms 上界用于暴露本机调度迟滞，不作为标准允许偏差。
