# 导入来源（import/pr7）

这个分支是对 Sparxie 主仓库两个 PR 的**忠实导入**，文件保持原路径和原内容，用于保留作者信息和便于对照。
插件化改造在 `feat/sparxie-plugin` 分支里做，不要在这个分支上改代码。

- PR #4「接入终末地 Runtime 会话控制」、PR #7「以挂起的 Games.exe 假父进程启动终末地」（#7 叠在 #4 之上）
- 来源仓库：`ShadowLemoon/Sparxie`；PR head：`skyhua0224/Sparxie` 的 `feat/sparxie-endfield-decoy`，提交 `77d6051`
- 作者：skyhua0224

导入的文件（只含终末地相关部分，其余主仓库改动没有导入）：

- `native/EndfieldCore/**`：原生运行库、核心、假父进程、测试与假游戏
- `src/Sparxie.Contracts/Models/EndfieldUiSettings.cs`、`src/Sparxie.Infrastructure/Endfield/EndfieldUiIni.cs`、
  `src/Sparxie.SessionHost/Sessions/EndfieldGameController.cs`、`EndfieldUiSync.cs`：C# 部分（插件化时用 C++ 重写）
- `tests/**/Endfield*.cs`：对应的 C# 测试（插件化时改写成原生测试）
- `build/capabilities.json`
