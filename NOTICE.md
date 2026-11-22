# 第三方声明

本插件的代码来自 skyhua0224 在 Sparxie 主仓库提交的 PR #4 / #7（见 `IMPORT-PR7.md`），并整理成 Sparxie 插件。
下面是它参考或移植的项目及其许可。**这份清单里"已核实"指我们在 2026-11-22 查看了对应仓库的许可文件和提交，不是法律意见。**

## 1. Collapse Launcher 的终末地插件（移植了其启动方法）

- 项目：Hi3Helper.Plugin.Endfield（含 Hi3Helper.Hypergryph.Core）
- 来源：https://github.com/misaka10843/Hi3Helper.Plugin.Endfield ，提交 `1efd304855ed68535d50e11504d790cd7004a3b1`
  （已核实该提交存在，并包含 `Hi3Helper.Hypergryph.Core/Management/GameDecoyParentBridge.cs`、`ParentProcessBridge.cs`）
- 许可：MIT（已核实，仓库许可为 MIT），版权 Copyright (c) 2025 Collapse Launcher
- 用途：`module/core/endfield_decoy.cpp` 把其启动方法由 C# 移植为 C++：把游戏目录下同签名者的 `PlatformProcess.exe` 复制为 `Games.exe`
  并以挂起状态启动、不恢复，再以它为 `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` 创建 `Endfield.exe`；创建前后用 `WinVerifyTrust` 与签名者证书哈希比对两份映像。

```
MIT License

Copyright (c) 2025 Collapse Launcher

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 2. EndField.Fps.UnlockerIsland（只参考公开行为，未复制源文件）

- 来源：https://github.com/isxlan0/EndField.Fps.UnlockerIsland（该仓库已归档）
- 许可：仓库的 `LICENSE` 是 MIT，版权 Copyright (c) 2025 Xiao Lan（已核实）。
- **另有一句说明**：它的 README 末尾写着「This project is for learning and research purposes only and is prohibited for commercial use.」。
  这句话不在 `LICENSE` 文件里，但它表达了作者的意图。PR 作者声明本插件"只参考公开行为重新实现了直接启动、目标帧率热调和注入流程，没有复制其源文件"；
  该声明**需要作者在公开分发前再次确认**。本插件只应免费分发。若将来复制了其代码或资源，必须保留其版权与许可声明。

## 3. Better-Endfield（只参考其公开文档的研究结论）

- 来源：https://github.com/Dr-hydra/Better-Endfield（`docs/ui/MOBILE_UI_REVERSING.md` 等文档）
- 许可：**AGPL-3.0**（已核实，仓库许可为 GNU Affero General Public License v3.0）。
- 本插件的运行库源码注释里引用了它的文档（"写入状态要走 `ChangeInputType`，且在主线程"）和它的 loader 的引导顺序（创建挂起、恢复、再远程 `LoadLibraryW`），
  注释声明"代码是我们自己写的"。**如果运行库里有任何从该项目复制或逐行改写的代码，AGPL-3.0 的义务会随之而来。**
  这一点只能由代码作者确认；确认之前，不要把本插件当作"许可已经清楚"的产物对外分发。

## 4. Sparxie 插件 ABI 头文件

- `third_party/sparxie-plugin-abi/sparxie_game_module.h`：MIT 许可，随头文件的 `LICENSE`，来自 Sparxie 主仓库 `native/plugin-abi`（提交 `3e61fc9`）。

## 5. 本仓库自身的许可

尚未决定，由代码作者（skyhua0224）选择；`plugin.json` 的 `license` 字段目前写的是"MIT（草案，待作者确认）"。
