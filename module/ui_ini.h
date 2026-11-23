// endfield-ui.ini 的读-改-写：EndfieldRuntime.dll 会隔几秒重读这个小文件。
// 只设置传入的键，其余行（注释、插件不认识的运行库键、空行、顺序）原样保留；文件原子替换，运行库永远读不到写了一半的文件；内容没变就不碰文件。
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace endfield_plugin {

using IniValues = std::vector<std::pair<std::string, std::string>>;

constexpr const wchar_t *kIniFileName = L"endfield-ui.ini";

// 纯函数：在 existing 里设置 values 之后的文本（测试主要打这里）。新键加到文件开头（注释块之后），因为运行库只读前 2047 字节。
std::string MergeIni(const std::string &existing, const IniValues &values);

// 键只能是字母数字下划线，值只能是字母数字和 . , - _ ：换行或第二个键进了值就会变成另一个设置。
bool IsSafeIniToken(const std::string &key, const std::string &value);

// 读文件、合并、原子写回。返回 true=写了文件，false=内容本来就一样。失败（含不安全的键值）返回 false 并设 error。
enum class IniResult { written, unchanged, failed };
IniResult ApplyIni(const std::wstring &path, const IniValues &values, std::wstring *error = nullptr);

}  // namespace endfield_plugin
