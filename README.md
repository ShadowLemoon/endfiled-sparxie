# endfiled-sparxie — 明日方舟：终末地 的 Sparxie 插件

把终末地的启动、帧率解锁、触控界面支持做成 [Sparxie](https://github.com/ShadowLemoon/Sparxie) 的**第三方游戏插件**（`ext.skyhua0224.endfield`），
不放进主程序。来源是 skyhua0224 在 Sparxie 主仓库提交的 PR #4 / #7，见 [`IMPORT-PR7.md`](IMPORT-PR7.md)。

> ## ⚠️ 风险提示（请先读）
>
> 这是**非官方**插件，与游戏公司无关。它会做下面这些事，**可能违反游戏的用户协议，并有被反作弊系统处理（包括封号）的风险**：
>
> - **向游戏进程注入代码**（运行库 `EndfieldRuntime.dll`）：保持触控输入类型、解锁帧率、改触控界面大小/安全边距。
> - **伪造游戏的父进程**：把游戏目录里厂商签名的 `PlatformProcess.exe` 复制成 `Games.exe`，以挂起状态启动，再把它设为 `Endfield.exe` 的父进程。
> - **向游戏声明"云游戏/移动端"环境**（输入类型、用户平台）。
> - 需要**管理员权限**，并会在游戏目录里创建临时的父进程暂存目录。
>
> 只启用你信任其来源的版本。Sparxie 在启用前会显示本插件 DLL 的哈希并要求你确认；它**没有沙箱**。
> 许可状态见 [`NOTICE.md`](NOTICE.md)（其中有两项需要代码作者确认后才适合公开分发）。

## 状态

- 第一版 `0.1.0`：**开发中**。原生测试和 ABI 合规测试通过；**没有在真实游戏上验证过**（需要作者验证：签名校验、假父进程过客户端检查、注入时机、
  触控保持、帧率、云模式路径 A/B、缩放/安全边距实际效果、管理员权限）。
- 已知限制：运行中改"触控界面缩放/安全边距"不会实时生效（下次启动才生效）。要等 Sparxie 主程序提供"运行中推送设置"（跟踪项 `plugin-9`）后再接上。
- `parent_source_path`（自定义假父进程来源）**不对用户开放**：只用游戏目录下的 `PlatformProcess.exe`，避免被指定成任意 EXE。

## 目录

```
plugin.json              插件元数据 + 设置 schema（Sparxie 据此生成设置页）
module/                  module.dll 的源码：ABI 适配层（spx_module）、设置解析、ini 读写、运行库嵌入与解压
module/core/             启动、假父进程、注入（来自 PR #7；静态库）
runtime/                 注入游戏进程的运行库 EndfieldRuntime.dll（构建后嵌进 module.dll）
tests/native/            原生测试（PR 原有 + 设置/ini/ABI 合规测试）和假游戏
third_party/             Sparxie 插件 ABI 头文件（MIT）
docs/                    运行库的契约文档（云模式、endfield-ui.ini）
```

Sparxie 配置里的附加启动参数（和无边框开关生成的 `-popupwindow`）原样追加到游戏命令行的屏幕参数之后（`plugin.json` 声明了 `needsArgs`）。游戏是否接受某个参数没有验证过。

## 构建与测试（Windows，Visual Studio 2026 + CMake）

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

产物在 `build\bin\Release\`：`module.dll`（唯一导出 `sparxie_module_query`，运行库已嵌在里面）、`EndfieldRuntime.dll`（仅用于测试，发布不需要）。

## 打包与发布

校验、打包和发布脚本在 [sparxie-dev/plugin-tools](https://github.com/sparxie-dev/plugin-tools)，本仓库只有两个调用它的工作流（`validate.yml`、`release.yml`）。

- push / PR：校验 `plugin.json`，编译、跑原生测试、打包，并加载编出来的 `module.dll` 和 `plugin.json` 核对。
- 发布：把 `plugin.json` 的 `version` 和 `module/spx_module.h` 的 `kPluginVersion` 一起改成新版本，合进 `main` 后推 tag `v<版本>`。工作流会创建带 SHA-256 的 GitHub Release；版本号带 `-`（如 `0.2.0-rc.1`）标成预发布。同名 Release 已存在会直接失败，要改版本号重发。

本地想打包（需要 Python 3.11+）：

```powershell
git clone --depth 1 --branch v1 https://github.com/sparxie-dev/plugin-tools.git .plugin-tools
python .plugin-tools/scripts/build_plugin.py --repo .
```

得到 `dist\ext.skyhua0224.endfield-<版本>.zip` 和 SHA-256 清单。在 Sparxie 设置页「第三方游戏插件」里导入这个 zip（或拖进去），确认后启用。

## 设计要点

- **运行库嵌在 module.dll 里**：Sparxie 里用户确认的哈希只覆盖 `module.dll` 和 `plugin.json`。运行库如果放在旁边，被替换了宿主发现不了，
  所以会话开始时把它解压到 `<data_dir>\runtime\<哈希前 12 位>\`，重新校验哈希，并一直持有一个拒绝写入的句柄到会话结束。
- **所有设置只经 `settings_json` 传入**，由 Sparxie 按 `plugin.json` 的 schema 归一化；插件里再做一遍同样的归一化（宿主不规范时也不会崩）。
- **生产路径永远要求厂商签名**：测试开关（跳过签名检查、只启动不注入）只在仓库内的原生测试里使用，`module.dll` 里 `flags` 固定为 0。

## 对照 Sparxie 宿主验证

`tests/host/HostCompatTests.cs.txt` 是一个可以复制进 Sparxie 主仓库 `tests/Config.Tests` 的测试：用宿主自己的 manifest 解析、插件目录、`NativePluginLibrary`（`get_info` 与 `plugin.json` 交叉核对）和会话层加载本插件，
并确认没有内嵌厂商签名的游戏在假父进程这一步被安全拒绝。用法写在文件头。

## 背景

`plugin.json` 的 `backgroundApi` 请求鹰角启动器的 `get_main_bg_image` 接口（国服参数：appcode / channel / seq），Sparxie 主程序按声明取出图片和视频地址。参数来自对公开启动器接口的观察，并参考了 Collapse 的 [Hi3Helper.Plugin.Hypergryph](https://github.com/misaka10843/Hi3Helper.Plugin.Hypergryph)。国际服 / B 服的参数不同（见该项目的 Endfield 预设），目前只配了国服。
