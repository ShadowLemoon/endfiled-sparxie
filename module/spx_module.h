// Sparxie 游戏模块 ABI 适配层：把 endfield_core（内部 C++ 接口）包成 sparxie_game_module.h 定义的函数表。
#pragma once

#include "sparxie_game_module.h"

namespace endfield_plugin {

// 函数表（静态存储期）。module_main.cpp 的导出函数返回它；测试可以不经 DLL 直接用。
const SpxModuleApi *GetModuleApi();

// 插件 id / 版本（plugin.json 必须与之一致，发布工作流加载 module.dll 后会核对）
constexpr const wchar_t *kPluginId = L"ext.skyhua0224.endfield";
constexpr const wchar_t *kPluginVersion = L"0.1.1";

}  // namespace endfield_plugin
