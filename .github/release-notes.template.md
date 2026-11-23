## {{DISPLAY_NAME}} {{VERSION}}

Sparxie 的第三方游戏插件（`{{PLUGIN_ID}}`），非官方。

### 安装

1. 下载下面的 `{{ZIP_NAME}}`。
2. 在 Sparxie 设置的「第三方游戏插件」里点“导入插件…”选这个 zip（也可以直接把 zip 拖进去）。
3. 在列表里点“查看并启用”，核对哈希后确认。

### 校验（SHA-256）

| 文件 | SHA-256 |
|---|---|
| module.dll | `{{DLL_SHA256}}` |
| plugin.json | `{{MANIFEST_SHA256}}` |
| {{ZIP_NAME}} | `{{ZIP_SHA256}}` |

Sparxie 的启用确认框里显示的是 module.dll 和 plugin.json 的哈希，请和上表对照；不一致就不要启用。

### 风险提示

插件会向游戏进程注入代码并以管理员权限运行，可能违反游戏用户协议并有被封号的风险，请只启用你信任来源的版本。
许可与第三方声明见包里的 `NOTICE.md`。

### 本版本改动

{{CHANGES}}
