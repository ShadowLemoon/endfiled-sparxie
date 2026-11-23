# 发布流程

发一个新版本只需要：改版本号、合进 `main`、推一个 tag。构建、测试、打包、发布说明和 GitHub Release 都由 `.github/workflows/release.yml` 完成。

## 一、发布前检查

1. **版本号**：同时改两处，`scripts/verify_manifest.ps1` 会检查它们一致，不一致构建就失败：
   - `plugin.json` 的 `version`
   - `module/spx_module.h` 里的 `kPluginVersion`
2. **manifest 内容**：`exeNames`、`caps`、`settingsSchema`、`autoFind`、`backgroundApi` 等有变化时，对照 Sparxie 的 `docs/PLUGINS.md` 检查。能力（`caps`）新增会在用户的确认框里显示为“新增能力”。
3. **NOTICE.md**：新增了第三方代码、图标或素材时写明来源和许可；有尚未确认的许可问题时不要对外发布。
4. **本地跑一遍**（可选，但建议）：
   ```powershell
   powershell -File scripts\package.ps1
   ```
   会校验 `plugin.json`、编译、跑原生测试、打包到 `dist\`，并打印每个文件的 SHA-256。
5. 用 Sparxie 本地验证：把 `dist\<id>` 文件夹拖进设置里的“第三方游戏插件”，确认能启用、图标和设置项正常。真实游戏的成功路径只能在装了游戏的机器上人工验证。

## 二、发布

1. 把改动合进 `main`（CI 的 `build` 工作流会先跑一遍）。
2. 在 `main` 上打 tag，**tag 必须是 `v` 加上 `plugin.json` 的版本号**：
   ```powershell
   git tag v0.2.0
   git push origin v0.2.0
   ```
   预发布版本用带连字符的版本号，例如 `plugin.json` 写 `0.2.0-rc.1`、tag 为 `v0.2.0-rc.1`，Release 会自动标成预发布。
3. 等 `release` 工作流跑完。它会依次做：
   1. 检查 tag 格式，并与 `plugin.json` 的版本一致；
   2. 检查同名 Release 不存在；
   3. 校验 `plugin.json`、编译、跑原生测试、打包（和本地用的是同一个 `scripts/package.ps1`）；
   4. 解开 zip，检查 `module.dll`、`plugin.json`、`NOTICE.md`、`README.md`、`icon.ico` 都在，id 和版本与 tag 一致；
   5. 生成发布说明（安装步骤、`module.dll` / `plugin.json` / zip 的 SHA-256、风险提示、自上个 tag 以来的提交）；
   6. 创建 GitHub Release，附上 zip 和哈希清单；同时作为工作流产物保留。
4. 打开 Release 页面，核对说明里的哈希，再按需补充人工写的更新说明。

## 三、不要做的事

- **不要覆盖已经发布的文件。** `module.dll` 每次编译哈希都不同，换了文件，用户之前核对过的哈希就对不上了。工作流发现同名 Release 已存在会直接失败，要改版本号重发。
- 不要在本地构建后手工上传。发布出去的包应当来自 CI，这样哈希、版本和源码一一对应。
- 不要把测试开关（跳过签名校验、只启动不注入）编进发布的 `module.dll`，生产路径的 `flags` 固定为 0。

## 四、用户怎么安装

写在每个 Release 的说明里：下载 zip，在 Sparxie 设置的“第三方游戏插件”里导入（或直接拖进去），点“查看并启用”，对照说明里的哈希确认。更新时同样导入新 zip；`module.dll` 变了会显示“文件已变化”，需要重新确认。

## 五、手动重跑

`release` 工作流也支持手动触发（Actions 页面选 `release`，填已存在的 tag）。适用于 tag 已经推了但构建失败、还没有创建 Release 的情形；已经有 Release 时会失败，这是有意的。
