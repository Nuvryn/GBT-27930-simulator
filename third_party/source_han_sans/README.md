# 界面中文字体

来源：[Adobe Source Han Sans](https://github.com/adobe-fonts/source-han-sans/tree/2.004R)，版本 2.004R，SIL Open Font License 1.1，许可证见 LICENSE.txt。

原始文件：https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.004R/OTF/SimplifiedChinese/SourceHanSansSC-Regular.otf

原始文件 SHA-256：`84bbd4ace91d327b3ad1a581c688196278a4e41308520176f419180064e4af2b`。

`sim_ui_font_16.c` 是应用专用字形子集（派生字体名 `sim_ui_font_16`），由 LVGL 官方 MIT 许可的 lv_font_conv 1.5.3 生成，16 px、4 bpp、不压缩。包含 ui_chars.txt 中的界面字符及可打印 ASCII；图标回退到已有 Montserrat 14 字体。原始 OTF 不参与构建、无需部署。自定义配置中的任意中文字符串不保证覆盖，不提供中文输入法。

普通离线构建直接使用已提交的生成文件，不需要 Node.js 或下载字体。修改界面文案增加汉字时，使用：

```bash
npm install --prefix build/font-tools --ignore-scripts --no-audit --no-fund lv_font_conv@1.5.3
# 将上述固定版本 OTF 下载到 build/font-tools/SourceHanSansSC-Regular.otf
python3 scripts/generate_ui_font.py --font build/font-tools/SourceHanSansSC-Regular.otf --converter build/font-tools/node_modules/lv_font_conv/lv_font_conv.js
```

脚本校验原始字体哈希，提取界面用字，生成源码，并移除工作站绝对路径。重生成后同步更新依赖清单哈希。
