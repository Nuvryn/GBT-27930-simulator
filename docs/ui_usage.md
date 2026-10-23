# LVGL 触屏界面

## 构建和启动

LVGL 固定为 v9.6.0（`80ca777e37a2b176770726a02e07a6fb79ef0b39`），源码随仓库保存，构建不访问网络。使用 C-only 构建、软件渲染和需要的控件，不依赖 SDL、Qt、tslib、GPU 或显示服务器。

```bash
# Linux / WSL：启用 UI
cmake -G Ninja -DPLATFORM=linux -DSIM_UI=ON -S . -B build-ui
cmake --build build-ui -j
./build-ui/gbt27930_simulator --config config/charger_ui.json

# ARM Linux：使用相同 UI 源码交叉编译
cmake -G Ninja -DPLATFORM=linux -DSIM_UI=ON -DBUILD_TESTING=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake -S . -B build-armhf-ui
cmake --build build-armhf-ui -j
```

默认 `SIM_UI=OFF`，不编译或链接 LVGL。Zephyr 本轮仅支持 OFF 骨架编译；ON 会在配置阶段明确报错。每个构建目录继续导出 compile_commands.json。

运行开关独立于构建开关，配置示例：

```json
{
  "role": "charger",
  "version": 2015,
  "start_mode": "manual",
  "can_interface": "can0",
  "file_log": true,
  "ui": {
    "enabled": true,
    "framebuffer": "/dev/fb0",
    "input_device": "/dev/input/event1",
    "rotation": 0
  }
}
```

BMS 示例见 config/bms_ui.json；两个示例中的触摸路径都是占位示例，应替换为实际可用的输入设备。支持 2015/2023 A 类，version 可按需修改。

| ui 字段 | 默认值 | 说明 |
|---|---|---|
| enabled | false | 启用时必须使用 manual 模式及含 UI 的可执行文件 |
| framebuffer | /dev/fb0 | 非空，最多 255 字节，使用官方 fbdev 后端 |
| input_device | 空 | 最多 255 字节；启用 UI 时必须明确指定 evdev 设备 |
| rotation | 0 | 0/90/180/270 度，显示及指针坐标经 LVGL 处理 |

**分辨率、像素格式和行跨度来自系统显示设备，不在 config 中配置。** 800×480 只是设计参考，布局依实际宽高换行、滚动；不缩放或覆盖系统分辨率。软件测试覆盖 800×480 和 480×272。未实现任意尺寸屏幕保证、热插拔换屏或在应用内修改显示时序，也不涉及 BSP 开发。

UI 关闭时不打开显示/触摸设备，保留 Enter 及 auto 运行方式。UI 启用时不读取 stdin，因此可以没有控制台输入；桩端通过屏幕 Start 开始。请求启用却未编译 UI、配置非法或设备初始化失败均返回非零，不静默回退。初始化成功后才输出 READY。

## 页面和操作

- **Home**：角色/版本/CAN、会话编号、阶段/会话时间、参数修订号；流程节点分别显示 Ahead、Current、Visited、Error。另显示按实际事件顺序排列的路径、正在等待/监控的报文和就绪条件、最近错误及其报文。状态来自结构化协议快照，不解析日志或播放固定动画。节点与路径回到 Idle 后保留，下一轮重新开始；恢复和重复阶段按实际进入顺序显示。超过 64 次迁移明确提示旧历史被淘汰，文本路径展示最近 12 次。
- **Parameters**：仅编辑本角色的标量字段，按报文注册顺序排列，显示范围和步长。V/A 精确转换为 mV/mA 整数，不使用浮点舍入接受非法值。其他单位见字段键；枚举及跨字段约束仍通过原校验器。单体数组及字符串等复杂字段继续由 JSON 提供。
- **Control**：桩端 Start charging 触发 CHM；BMS 无本地主动开始，等待合法对端 CHM。Stop charging 通过独立通道请求当前会话正常中止，不被普通命令队列挤占；过期会话请求不会停止另一轮。正常或异常处理结束后继续待机。Ctrl-C/SIGTERM 保留现有中止/退出语义。
- **Faults**：选择阶段、报文、tx/rx、动作、次数、持续时间、延迟、字节及替换值，Enable/Disable 控制一条临时手动规则。支持 drop/delay/duplicate/corrupt/length/id；TP_DT 的字节 0 是序号。替换值使用十进制。待机可预置下一轮规则，顶部显示 ARMED；运行中显示 ACTIVE 及命中次数。次数/持续时间耗尽显示 OFF。
- **Logs / Info**：版本、构建时间、目标、配置来源和日志/抓包开关；日志可滚动、暂停显示、按标签或文本筛选、清空显示。文字键盘与数字键盘按输入框切换，完成/取消收起键盘。

主页的本端数据标为 Local simulated；对端数据标为 Peer reported，仅在相关报文已合法接收且未过期时显示。失效或未收到的数据标为 unavailable/expired，不能当作采集值。硬件动作日志仍仅代表模拟动作；为使用内置英文字体，运行期动作日志统一为英文，测试中的暂停/恢复证据同步使用英文。

## 参数、生效和保存

启动配置是初始值。待机编辑后点击 Apply for next session，由协议线程校验并替换下一轮参数，成功后增加修订号；充电时编辑禁用，Apply 请求也会在协议层拒绝。BMS 自动启动使用最近一次已确认 Apply 的值，未提交输入不影响协议。

每轮引擎复制参数。运行中改变报文统一使用错误注入，不直接修改本轮配置。控制请求带编号并给出结果；队列容量为 8，满或短时繁忙提示 Busy，可重试，协议线程不等待 UI 锁。

Save configuration 仅保存已 Apply 的配置，未 Apply 时禁用。保存由独立工作线程执行，同目录临时文件完成序列化、解析回读校验、刷新后原子替换启动配置；保留角色、版本、数组、配置 faults/events 等完整字段，不保存手动临时注入。写入或校验失败保持原文件，错误可见；原子替换后的目录刷新失败会报告持久化错误，不能保证掉电后的内容。没有文件来源时不提供保存。界面显示已应用/已保存修订号及保存结果。

每轮开始通过工作线程输出 SESSION_CONFIG_BEGIN、带会话/修订号的 SESSION_CONFIG 片段及 SESSION_CONFIG_END，文件日志中可以还原当轮有效配置；不会阻塞 BMS 握手等待文件 I/O。UI 关闭时沿用原启动快照记录。

## 注入及日志隔离

手动规则优先于配置规则，配置规则保持原有声明顺序；命中日志包含 source=manual/config、规则编号及修改内容。取消或替换手动规则会移除其尚未派发的延迟任务，不清除配置规则任务，已经发送的帧不可撤回。本轮结束后临时规则清理，下一轮默认关闭；Idle 中重新预置的规则仅作用于后续新会话。

UI 日志缓存预算为 256 KiB，使用 224 条、每条最多 1152 字节的固定记录槽，展示淘汰/争用跳过的数量；控件显示文本另有 32 KiB 上限，移除最旧完整行。DETAIL 配置片段仍仅进入文件证据。暂停、筛选、清空及缓存淘汰不改变文件输出；file_log=false 时仍可查看屏幕日志。文件日志和 candump 保持原配置及最近三次进程启动的保留策略，不在 UI 中运行期重开文件。

## 线程与验证

协议线程独占引擎/TP/注入器；UI 线程独占 LVGL；保存及会话证据由独立工作线程处理，EasyLogger 继续异步输出。UI/保存线程使用 Linux 普通调度策略。快照界面按 100 ms 更新，显示任务约 5 ms 推进一次 LVGL；CAN 协议周期由原协议线程管理。

```bash
# 无 UI 完整回归（需要已准备 vcan0）
./scripts/verify_simulator.sh --prepare-vcan

# UI 完整回归、可选 ARM UI 构建
./scripts/verify_ui.sh --prepare-vcan
./scripts/verify_ui.sh --arm
```

sim_control 验证参数、保存往返、队列和注入任务归属；control_concurrency 验证真实线程下 2000 个请求及结果顺序。sim_ui 使用 LVGL 内存显示和指针输入覆盖两版双角色、多轮、停止、参数 Apply、注入取消、暂停恢复和失败轨迹，并输出 PPM 图像。

ui_vcan 运行正式入口、UI 线程及实际 SocketCAN 的两个子进程，仅用测试专用内存显示替换物理屏。界面按钮触发两轮，检查有效配置证据、真实 DT 间隔及 BCL/CCS 周期，不放宽原有时序判据。该替换只存在于测试可执行文件，不加入正式程序的 config。

以上软件测试不替代真实显示/触摸、旋转校准和实物桩/BMS 联调；没有设备时相应项目保持 SKIP。
