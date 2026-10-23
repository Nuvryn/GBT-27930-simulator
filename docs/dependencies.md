# 第三方组件登记

以下组件均经用户确认采用，固定源码随仓库保存，构建不访问网络。

| 组件 | 来源 | 固定提交 | 许可证 | 用途 |
|---|---|---|---|---|
| LVGL | https://github.com/lvgl/lvgl | `80ca777e37a2b176770726a02e07a6fb79ef0b39`（v9.6.0） | MIT 及上游附带版权说明，`third_party/lvgl/LICENCE.txt`、`COPYRIGHTS.md` | 可选本机触屏 UI，C-only 软件渲染 |
| cJSON | https://github.com/DaveGamble/cJSON | `6d9f2443ab071f86e5d9b43025a40929ec41c46c` | MIT，`third_party/cjson/LICENSE` | 配置解析 |
| EasyLogger | https://github.com/armink/EasyLogger | `806328e131836662fa83dd43364a53f699fd76ac` | MIT，`third_party/easylogger/LICENSE` | 日志格式化、异步缓冲与线程输出 |
| Open-SAE-J1939 | https://github.com/DanielMartensson/Open-SAE-J1939 | `2a890c58c6a199d6a6ef793b71bac3c53f594823` | MIT，`third_party/open_sae_j1939/LICENSE` | TP 报文生成与接收存储 |

cJSON 只导入 `cJSON.c`、`cJSON.h`、LICENSE；EasyLogger 导入头文件、日志核心、异步环形缓冲 elog_async.c、工具源码及 LICENSE；均来自原固定提交。
上游文件内容均未修改，保留原命名、注释和格式。换行比较统一为 LF。
`spec-exempt.txt` 仅豁免上述第三方目录；自有配置、平台后端和适配层均不豁免。

项目自己的 EasyLogger 配置位于 `app/logging/elog_cfg.h`，启用 ELOG_ASYNC_OUTPUT_ENABLE 和 64 KiB 缓冲。
不启用上游内置 pthread 路径，线程、通知、互斥与 join 由平台 log_sink 后端提供。生产者只格式化和入队，工作线程读缓冲后释放互斥再执行 I/O。
上游满缓冲会截断记录；自有包装在入队前检查待消费字节数，容量不足时拒绝整条、锁存 ENOSPC 并令会话失败。
CMake 仅对 elog_async.c 的编译符号做重命名以接入该检查，原始文件内容与固定哈希保持一致。
文件访问仅由 Linux `log_sink` 后端完成；Zephyr 文件输出返回不支持。
cJSON 编译时限制嵌套深度为 32，配置文件上限 64 KiB。

核验命令：`python3 scripts/check_dependencies.py`。新组件哈希见 [依赖哈希清单](../third_party/manifests/dependencies-manifest.json)；
Open-SAE-J1939 使用其单独的快照清单。升级时固定新提交、更新登记和哈希，并重跑协议与构建回归。
如需修改第三方文件，应单独记录补丁，调整豁免清单，再实施变更；本次未修改上游源码。

## 外部运行工具

用户指定的 `candump` 来自 [can-utils](https://github.com/linux-can/can-utils)。它以独立进程运行，不链接或复制源码到本工程，故不计入 vendored 文件哈希清单。Linux/ARM 系统启用抓包时需自行提供对应架构的工具；默认按 PATH 查找。WSL 验证包版本为 Ubuntu 的 2020.11.0-1。升级工具或目标镜像后需重跑抓包检查；构建不自动下载和安装该工具。

LVGL 导入 src、include、libs 及根公共头文件、许可证和配置模板，均保持上游内容不变。项目自己的 lv_conf.h 和平台适配位于自有目录；CMake 禁用外部绘制引擎，不启用或下载额外依赖。固定文件的 LF 归一化校验登记在统一依赖清单，SIM_UI=OFF 不构建 LVGL。

## UI 中文字体

用户确认采用思源黑体 Source Han Sans SC 2.004R，SIL OFL 1.1 许可。仅保存界面使用的派生字形源码、字符清单及许可证；由官方 lv_font_conv 1.5.3（MIT，仅开发工具）生成。来源、原始字体哈希及生成方法见 third_party/source_han_sans/README.md。


## Web 镜像依赖

用户确认采用 LibVNCServer 0.9.15（提交 `9b54b1ec32731bd23158ca014dc18014db4194c3`，GPL-2.0-or-later）与 noVNC 1.7.0（提交 `63107bd06d9e1f6136ff21aeda8cd62cbf0d433e`，主要 MPL-2.0；附带组件许可证见各目录）。固定源码子集原样存放于 third_party/libvncserver 和 third_party/novnc，许可证随库保存，文件哈希进入 dependencies-manifest.json。

`cmake/web_ui.cmake` 只构建所需 C 服务端及内置密码摘要/WebSocket代码，关闭可选 TLS、JPEG、zlib 和内部线程依赖；由 Linux 适配层提供独立网络线程。上游 HTTP 源文件单独通过编译宏把 fopen 映射到只读内嵌资源查找，继续复用原 HTTP 解析器，不修改固定源码、不读取任意板端文件。`scripts/embed_web_assets.py` 使用 Python 标准库在构建时打包本地网页和 noVNC JS，无运行时脚本需求。

仅 SIM_UI=ON 链接这些组件。LibVNCServer 的 GPL 要求适用于链接程序的分发，noVNC 与附带组件的许可声明须保留。精确来源和哈希以清单为准。
