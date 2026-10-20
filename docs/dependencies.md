# 第三方组件登记

以下组件均经用户确认采用，固定源码随仓库保存，构建不访问网络。

| 组件 | 来源 | 固定提交 | 许可证 | 用途 |
|---|---|---|---|---|
| cJSON | https://github.com/DaveGamble/cJSON | `6d9f2443ab071f86e5d9b43025a40929ec41c46c` | MIT，`third_party/cjson/LICENSE` | 配置解析 |
| EasyLogger | https://github.com/armink/EasyLogger | `806328e131836662fa83dd43364a53f699fd76ac` | MIT，`third_party/easylogger/LICENSE` | 同步日志格式与输出 |
| Open-SAE-J1939 | https://github.com/DanielMartensson/Open-SAE-J1939 | `2a890c58c6a199d6a6ef793b71bac3c53f594823` | MIT，`third_party/open_sae_j1939/LICENSE` | TP 报文生成与接收存储 |

cJSON 只导入 `cJSON.c`、`cJSON.h`、LICENSE；EasyLogger 只导入头文件、同步日志与工具源码及 LICENSE。
上游文件内容均未修改，保留原命名、注释和格式。换行比较统一为 LF。
`spec-exempt.txt` 仅豁免上述第三方目录；自有配置、平台后端和适配层均不豁免。

项目自己的 EasyLogger 配置位于 `app/logging/elog_cfg.h`，关闭异步与缓存模式，所有调用均在协议线程执行。
文件访问仅由 Linux `log_sink` 后端完成；Zephyr 文件输出返回不支持。
cJSON 编译时限制嵌套深度为 32，配置文件上限 64 KiB。

核验命令：`python3 scripts/check_dependencies.py`。新组件哈希见 `dependencies-manifest.json`；
Open-SAE-J1939 使用其单独的快照清单。升级时固定新提交、更新登记和哈希，并重跑协议与构建回归。
如需修改第三方文件，应单独记录补丁，调整豁免清单，再实施变更；本次未修改上游源码。
