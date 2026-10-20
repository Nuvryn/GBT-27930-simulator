# GBT-27930-simulator
GB/T 27930 协议模拟器，用于桩端/BMS 端协议一致性测试。

当前已接入平台抽象层，协议状态机、配置解析和错误注入业务尚未实现。

## 目录

| 目录 | 职责 |
|---|---|
| `osal/` | 任务、互斥锁、信号量、消息队列、单调时钟与内存接口 |
| `hal/` | CAN、UART、SPI、I2C、GPIO 等硬件接口契约 |
| `support/` | 配置源、启动参数、控制台与确定性 CAN 收发接口 |
| `backends/` | POSIX/Linux 后端及 Zephyr 骨架 |
| `app/` | 当前为平台验证示例，后续承载模拟器业务 |
| `cmake/` | ARM 工具链及 Zephyr 编译检查桩头文件 |
| `config/` | 示例配置文本，当前只验证加载，不解析业务字段 |
| `tests/` | CAN 收发集成测试 |

分层约定见 [架构规范](docs/architecture-spec.md)，移植范围及已知限制见 [移植说明](docs/platform-port.md)。

## 构建与验证

在 Linux 或 WSL 中，从工程根目录运行（需要 CMake、Ninja、GCC）：

```bash
bash scripts/verify_platform.sh
./build/scaffold_demo
./build/scaffold_demo --config config/default.json
```

默认生成 `build/compile_commands.json`，clangd 可通过 `--compile-commands-dir=build` 使用。
示例加载配置、探测 `can0` 并输出 5 次 tick；CAN 探测不到只输出信息，不代表 CAN 测试通过。
配置加载失败返回 1。

CAN 测试通过独立 SocketCAN socket 验证适配器的扩展帧发送、接收、数据内容及非法长度。
无 `vcan0` 时返回 **77（SKIP）**；有接口但打开或收发失败时返回非零失败码。
可先以有网络管理权限的用户准备虚拟接口并测试：

```bash
sudo bash scripts/verify_vcan.sh
# sudo 不可用时：
unshare -Urn bash scripts/verify_vcan.sh
```

WSL 重启后需重新准备虚拟接口。当前未提供协议双进程对跑的 51 场景测试。

ARM Linux 交叉编译（需要 `arm-linux-gnueabihf-gcc`）：

```bash
bash scripts/build-armhf.sh
bash scripts/build-armhf.sh verify
```

Zephyr 仅保留抽象层框架；以下命令只验证桩头文件下的编译，不表示支持真实设备运行：

```bash
cmake -G Ninja -DPLATFORM=zephyr -S . -B build-zephyr
cmake --build build-zephyr -j
```
