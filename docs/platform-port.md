# 平台抽象层移植说明

## 来源与范围

2026-10-20 从本机 `D:\BaiduSyncdisk\coding_test\platform-framwork-template` 导入。
来源未包含 Git 元数据，按文件 SHA-256 记录快照，见同目录 `platform-import-manifest.json`。
该清单记录源文件哈希和导入路径；后续同步时重新计算本地文件哈希进行比较。

导入 `osal/`、`hal/`、`support/`、`backends/`、`cmake/`，以及最小示例、配置、构建脚本和架构规范。
模板的 OSAL/HAL/support 接口和后端保持原样；原项目的协议资料、许可证和编码规范保留。
模板的空 `third_party/` 未导入，也未引入额外依赖库。

## 项目适配

- CMake 工程名改为 `gbt27930_simulator`，默认导出编译数据库；保留模板库名 `scaffold_osal`、`scaffold_hal`、`scaffold_support` 和验证程序名 `scaffold_demo`，便于逐文件同步。
- 为三个平台库开启 `-Wall -Wextra`；新增 CAN 测试启用额外转换告警并视为错误。
- 最小示例配置加载失败返回 1；新增 CTest 及脚本检查，防止关键初始化失败被报告为成功。
- ARM 构建明确使用 Ninja、Linux 后端和编译数据库。
- 增加 `test_can_hal`，通过独立 SocketCAN socket 检查扩展帧双向收发；缺少接口返回 77，接口存在时的打开失败属于失败。
- `.gitattributes` 沿用模板的 LF 规则，`.gitignore` 追加构建和日志产物。
- 修复原 `.clang-format` 尾部多余的文档分隔符，并补齐项目规范建议的 include/注释顺序和短函数设置；仅格式化本次调整的应用及新增测试，导入的接口与后端保持原样。

## 分层及规范取舍

接口位于根目录三条契约轴，平台实现位于对应 `backends/`，应用经契约访问平台能力。
CMake 按 `PLATFORM` 选择文件组；应用只在入口签名处区分平台。
未来协议模块使用 `support/can_bus.h` 收发，在应用线程解析报文。

`architecture-spec.md` 和 `support/README.md` 为模板原始文档。
其中句柄 `void *`、负 errno 返回值等为导入接口既有约定；为保持兼容本次保留，
并非声明满足 `embedded-c-coding-spec.md` 的全部推荐条款。
导入代码未进行完整 MISRA 审核，也未以第三方代码名义整体豁免；后续修改应按项目规范评审。
Linux HAL 的历史修补说明保留在 `backends/hal/linux/DEVIATIONS.md`，该文件描述模板相对其更早来源的差异。

## 当前能力边界

- Linux 后端具备实际系统调用实现；硬件外设仍需相应设备节点、驱动和权限，本次只验证 CAN 虚拟接口。
- Zephyr 使用 `cmake/zephyr_stubs` 做骨架编译，不验证 SDK、板卡、驱动或运行行为。
- CAN 适配器为进程内单总线实例；打开、关闭由应用线程串行调用，不支持多个线程并发管理生命周期。
- HAL 没有公共设备销毁接口；`can_bus_close()` 解除过滤器并重置适配器状态，底层资源最终由进程退出回收。当前适合每个进程模拟一个角色。
- 原模板启动参数解析、配置文件读取及各外设的边界行为仍需后续专项审核；配置当前只作为文本加载。
- 目前示例和 CAN 集成测试用于证明移植接通，不包含 GB/T 27930 编解码、状态机、超时测试或错误注入。
