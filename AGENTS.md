# 项目说明

当前工程为GB/T 27930协议模拟器，用于模拟27930协议进行协议一致性测试

适用平台：arm linux / zephyr（zephyr平台暂时不实现，只保留抽象层框架）（测试时可在wsl通过虚拟can进行编译测试以及程序对跑测试）

## 功能

- 可通过配置项选择：模拟角色（充电桩/bms）
- 可通过配置项选择：27930协议版本（2015/2023）
- 通过配置项进行桩端/bms充电参数配置（例如充电最大电压、需求电压等）
- 通过配置项进行协议参数配置（例如报文超时时间进行超时测试）
- 通过配置项进行错误注入（例如报文超时、报文数据有误等）
- 通过配置项选择日志是否存储文件



# 充电功能标准参考

充电相关标准参考

- ./docs/GBT-27930-2015-协议整理.md
- ./docs/GBT-27930-2023-A类协议整理.md
- ./docs/GBT-27930-2023-传输协议TP功能说明.md

# 编译方式

当前工程采用cmake+ninja+arm linux gcc进行构建（使用clangd，切记编译时要生成compile_command.json文件，zephyr平台暂时不用管。测试时可使用pc上的wsl上的工具链进行编译，直接在wsl中创建虚拟can卡，分别用两个进程扮演充电桩和bms端进行协议对跑。

```bash
cmake -DPLATFORM=linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build && cmake --build build -j
./scripts/build-armhf.sh                                            # ARM 交叉
cmake -DPLATFORM=zephyr -S . -B build-zephyr && cmake --build build-zephyr -j   # 骨架校验
```

- `compile_commands.json` 由 `CMakeLists.txt` 的 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`（第 24 行）**默认导出**；上面命令里显式写 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 是冗余保险，clangd 索引依赖该文件。

# 测试与回归

```bash
./build/test_can_hal                  # 无 vcan0 时返回 77(SKIP)，不是 0，判据必须区分
SIM_SPEED=10 ./tests/test_runner.sh    # 51 个场景，每场景两个进程对跑
```

- vcan 准备：`sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up`；sudo 不可用时改用 `unshare -Urn bash -c 'ip link add dev vcan0 type vcan && ip link set vcan0 up && <命令>'`。**vcan 不跨 WSL 重启存活**，先 `ip link show vcan0`。
- **取退出码必须写成 `.sh` 文件再 `wsl -d <distro> -- bash /mnt/d/.../x.sh`**：`wsl ... bash -lc '...; echo $?'` 里的 `$?` 会被引号处理吃掉（实测 `false` 也报 0）。
- 模拟器 `can_bus_open` 失败时**退出码仍为 0**，脚本判据除退出码外还需 grep 日志中的 `can_bus_open failed`。

# 编码规范

参考./docs/embedded-c-coding-spec.md

# 提交规范

每次改动要有commit可追溯，以改动的功能进行分类。

参考./docs/

# 其他文档
